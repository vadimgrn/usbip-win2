#include "vhub.h"
#include <wdm.h>
#include "trace.h"
#include "vhub.tmh"

#include "dev.h"
#include "usbdsc.h"
#include <usbip\vhci.h>

#include <intrin.h>
#include <ws2def.h>
#include <ntstrsafe.h>

namespace
{

static_assert(sizeof(ioctl_usbip_vhci_plugin::service) == NI_MAXSERV);
static_assert(sizeof(ioctl_usbip_vhci_plugin::host) == NI_MAXHOST);

void to_ansi_str(char *dest, USHORT len, const UNICODE_STRING &src)
{
	ANSI_STRING s{ 0, USHORT(len - 1), dest };

	if (auto err = RtlUnicodeStringToAnsiString(&s, &src, false)) {
		Trace(TRACE_LEVEL_ERROR, "RtlUnicodeStringToAnsiString('%!USTR!') %!STATUS!", &src, err);
	}

	RtlZeroMemory(s.Buffer + s.Length, len - s.Length);
}

} // namespace


PAGEABLE vpdo_dev_t *vhub_find_vpdo(vhub_dev_t &vhub, int port)
{
	PAGED_CODE();

	if (!is_valid_rhport(port)) {
		return nullptr;
	}

	ExAcquireFastMutex(&vhub.mutex);

	auto vpdo = vhub.vpdo[port - 1];
	if (vpdo) {
		NT_ASSERT(vpdo->port == port);
	}

	ExReleaseFastMutex(&vhub.mutex);
	return vpdo;
}

PAGEABLE bool vhub_attach_vpdo(vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	NT_ASSERT(!vpdo->port);
	auto vhub = vhub_from_vpdo(vpdo);

	ExAcquireFastMutex(&vhub->mutex);

	for (int i = 0; i < vhub->NUM_PORTS; ++i) {
		auto &ptr = vhub->vpdo[i];
		if (!ptr) {
			ptr = vpdo;
			vpdo->port = i + 1;
			NT_ASSERT(is_valid_rhport(vpdo->port));
			break;
		}
	}

	ExReleaseFastMutex(&vhub->mutex);

	TraceMsg("%04x, port %d", ptr4log(vpdo), vpdo->port);
	return vpdo->port;
}

PAGEABLE void vhub_detach_vpdo(vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	TraceMsg("%04x, port %d", ptr4log(vpdo), vpdo->port);

	if (!vpdo->port) { // was not attached
		return;
	}

	NT_ASSERT(is_valid_rhport(vpdo->port));
	auto vhub = vhub_from_vpdo(vpdo);

	ExAcquireFastMutex(&vhub->mutex);
	{
		auto i = vpdo->port - 1;
		NT_ASSERT(vhub->vpdo[i] == vpdo);
		vhub->vpdo[i] = nullptr;
	}
	ExReleaseFastMutex(&vhub->mutex);

	vpdo->port = 0;
}

PAGEABLE NTSTATUS vhub_get_information_ex(vhub_dev_t &vhub, USB_HUB_INFORMATION_EX &p)
{
	PAGED_CODE();

	p.HubType = UsbRootHub;
	p.HighestPortNumber = vhub.NUM_PORTS;
	RtlZeroMemory(&p.u, sizeof(p.u));

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhub_get_port_connector_properties(vhub_dev_t&, USB_PORT_CONNECTOR_PROPERTIES &r, ULONG &outlen)
{
	PAGED_CODE();

	if (!is_valid_rhport(r.ConnectionIndex)) {
		return STATUS_INVALID_PARAMETER;
	}

	if (outlen < sizeof(r)) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	outlen = sizeof(r);

	r.ActualLength = sizeof(r);

	r.UsbPortProperties.ul = 0;
	r.UsbPortProperties.PortIsUserConnectable = true;
	r.UsbPortProperties.PortIsDebugCapable = true;
//	r.UsbPortProperties.PortHasMultipleCompanions = false;
//	r.UsbPortProperties.PortConnectorIsTypeC = false;

	r.CompanionIndex = 0;
	r.CompanionPortNumber = 0;
	r.CompanionHubSymbolicLinkName[0] = L'\0';

	return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS vhub_unplug_vpdo(vpdo_dev_t *vpdo)
{
	NT_ASSERT(vpdo);

	static_assert(sizeof(vpdo->unplugged) == sizeof(CHAR));

	if (InterlockedExchange8(reinterpret_cast<volatile CHAR*>(&vpdo->unplugged), true)) {
		Trace(TRACE_LEVEL_INFORMATION, "Device is already unplugged, port %d", vpdo->port);
		return STATUS_OPERATION_IN_PROGRESS;
	}

	Trace(TRACE_LEVEL_INFORMATION, "Unplugging device %04x on port %d", ptr4log(vpdo), vpdo->port);

	if (auto vhub = vhub_from_vpdo(vpdo)) {
		IoInvalidateDeviceRelations(vhub->pdo, BusRelations);
	}

	return STATUS_SUCCESS;
}

PAGEABLE void vhub_unplug_all_vpdo(vhub_dev_t &vhub)
{
	PAGED_CODE();

	ExAcquireFastMutex(&vhub.mutex);

	for (auto i: vhub.vpdo) {
		if (i) {
			vhub_unplug_vpdo(i);
		}
	}

	ExReleaseFastMutex(&vhub.mutex);
}

PAGEABLE NTSTATUS get_imported_devs(vhub_dev_t &vhub, ioctl_usbip_vhci_imported_dev *dev, size_t cnt)
{
	PAGED_CODE();
	TraceMsg("%!hci_version!, cnt %Iu", vhub.version, cnt);

	if (!cnt) {
		return STATUS_INVALID_PARAMETER;
	}

	ExAcquireFastMutex(&vhub.mutex);

	for (auto vpdo: vhub.vpdo) {

		if (!vpdo) {
			continue;
		}

		if (!--cnt) {
			break;
		}

		dev->port = make_vport(vpdo->version, vpdo->port);
		NT_ASSERT(is_valid_vport(dev->port));

		RtlStringCbCopyA(dev->busid, sizeof(dev->busid), vpdo->busid);

		to_ansi_str(dev->service, sizeof(dev->service), vpdo->service_name);
		to_ansi_str(dev->host, sizeof(dev->host), vpdo->node_name);
		to_ansi_str(dev->serial, sizeof(dev->serial), vpdo->serial);

                dev->status = SDEV_ST_USED;

                if (auto d = &vpdo->descriptor) {
			dev->vendor = d->idVendor;
			dev->product = d->idProduct;
		}
		
		dev->devid = vpdo->devid;
		dev->speed = vpdo->speed;
		
		++dev;
	}

	ExReleaseFastMutex(&vhub.mutex);

	dev->port = 0; // end of mark
	return STATUS_SUCCESS;
}
