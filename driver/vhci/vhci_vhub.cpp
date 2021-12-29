#include "vhci_vhub.h"
#include "trace.h"
#include "vhci_vhub.tmh"

#include "vhci_dev.h"
#include "usbip_vhci_api.h"

namespace
{

PAGEABLE vpdo_dev_t *find_vpdo(vhub_dev_t *vhub, ULONG port)
{
	PAGED_CODE();

	for (auto entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		auto vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);
		if (vpdo->port == port) {
			return vpdo;
		}
	}

	return nullptr;
}

PAGEABLE void mark_unplugged_vpdo(vhub_dev_t *vhub, vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	if (vpdo->plugged) {
		Trace(TRACE_LEVEL_ERROR, "vpdo already unplugged: port: %u", vpdo->port);
		return;
	}

	vpdo->plugged = false;

	NT_ASSERT(vhub->n_vpdos_plugged > 0);
	--vhub->n_vpdos_plugged;

	IoInvalidateDeviceRelations(vhub->pdo, BusRelations);
	Trace(TRACE_LEVEL_INFORMATION, "the device is marked as unplugged: port: %u", vpdo->port);
}

} // namespace


PAGEABLE vpdo_dev_t *vhub_find_vpdo(vhub_dev_t *vhub, ULONG port)
{
	PAGED_CODE();

	ExAcquireFastMutex(&vhub->Mutex);
	
	auto vpdo = find_vpdo(vhub, port);
	if (vpdo) {
		vdev_add_ref((vdev_t*)vpdo);
	}

	ExReleaseFastMutex(&vhub->Mutex);
	return vpdo;
}

PAGEABLE CHAR vhub_get_empty_port(vhub_dev_t * vhub)
{
	PAGED_CODE();

	CHAR port = -1;
	ExAcquireFastMutex(&vhub->Mutex);

	for (CHAR i = 0; i < (CHAR)vhub->n_max_ports; ++i) {
		if (!find_vpdo(vhub, i)) {
			port = i;
			break;
		}
	}

	ExReleaseFastMutex(&vhub->Mutex);
	return port;
}

PAGEABLE void vhub_attach_vpdo(vhub_dev_t *vhub, vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	ExAcquireFastMutex(&vhub->Mutex);

	InsertTailList(&vhub->head_vpdo, &vpdo->Link);
	++vhub->n_vpdos;
	if (vpdo->plugged) {
		++vhub->n_vpdos_plugged;
	}

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE void vhub_detach_vpdo(vhub_dev_t *vhub, vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	ExAcquireFastMutex(&vhub->Mutex);

	RemoveEntryList(&vpdo->Link);
	InitializeListHead(&vpdo->Link);
	NT_ASSERT(vhub->n_vpdos > 0);
	--vhub->n_vpdos;

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE void vhub_get_hub_descriptor(vhub_dev_t *vhub, USB_HUB_DESCRIPTOR *d)
{
	PAGED_CODE();

	d->bDescriptorLength = 9;
	d->bDescriptorType = USB_20_HUB_DESCRIPTOR_TYPE; // USB_30_HUB_DESCRIPTOR_TYPE
	d->bNumberOfPorts = (UCHAR)vhub->n_max_ports;
	d->wHubCharacteristics = 0;
	d->bPowerOnToPowerGood = 1;
	d->bHubControlCurrent = 1;
}

PAGEABLE NTSTATUS vhub_get_information_ex(vhub_dev_t *vhub, USB_HUB_INFORMATION_EX *p)
{
	PAGED_CODE();

	p->HubType = UsbRootHub;
	p->HighestPortNumber = (USHORT)vhub->n_max_ports;

	vhub_get_hub_descriptor(vhub, &p->u.UsbHubDescriptor);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhub_get_capabilities_ex(vhub_dev_t * vhub, PUSB_HUB_CAPABILITIES_EX p)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vhub);

	p->CapabilityFlags.ul = 0xffffffff;
	p->CapabilityFlags.HubIsHighSpeedCapable = FALSE;
	p->CapabilityFlags.HubIsHighSpeed = FALSE;
	p->CapabilityFlags.HubIsMultiTtCapable = TRUE;
	p->CapabilityFlags.HubIsMultiTt = TRUE;
	p->CapabilityFlags.HubIsRoot = TRUE;
	p->CapabilityFlags.HubIsBusPowered = FALSE;

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhub_get_port_connector_properties(vhub_dev_t *vhub, USB_PORT_CONNECTOR_PROPERTIES *p, ULONG *poutlen)
{
	PAGED_CODE();

	if (p->ConnectionIndex > vhub->n_max_ports) {
		return STATUS_INVALID_PARAMETER;
	}
	
	if (*poutlen < sizeof(*p)) {
		*poutlen = sizeof(*p);
		return STATUS_BUFFER_TOO_SMALL;
	}

	p->ActualLength = sizeof(*p);
	p->UsbPortProperties.ul = 0xffffffff;
	p->UsbPortProperties.PortIsUserConnectable = TRUE;
	p->UsbPortProperties.PortIsDebugCapable = TRUE;
	p->UsbPortProperties.PortHasMultipleCompanions = FALSE;
	p->UsbPortProperties.PortConnectorIsTypeC = FALSE;
	p->CompanionIndex = 0;
	p->CompanionPortNumber = 0;
	p->CompanionHubSymbolicLinkName[0] = L'\0';

	*poutlen = sizeof(*p);
	return STATUS_SUCCESS;
}

PAGEABLE void vhub_mark_unplugged_vpdo(vhub_dev_t *vhub, vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	ExAcquireFastMutex(&vhub->Mutex);
	mark_unplugged_vpdo(vhub, vpdo);
	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE void vhub_mark_unplugged_all_vpdos(vhub_dev_t * vhub)
{
	PAGED_CODE();

	ExAcquireFastMutex(&vhub->Mutex);

	for (auto entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		auto vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);
		mark_unplugged_vpdo(vhub, vpdo);
	}

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE NTSTATUS vhub_get_ports_status(vhub_dev_t * vhub, ioctl_usbip_vhci_get_ports_status *st)
{
	PAGED_CODE();

	Trace(TRACE_LEVEL_INFORMATION, "Enter");

	RtlZeroMemory(st, sizeof(*st));
	ExAcquireFastMutex(&vhub->Mutex);

	for (auto entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		auto vpdo = CONTAINING_RECORD (entry, vpdo_dev_t, Link);
		if (vpdo->port >= 127) {
			Trace(TRACE_LEVEL_ERROR, "strange port");
			continue;
		}
		st->port_status[vpdo->port] = 1;
	}

	ExReleaseFastMutex(&vhub->Mutex);

	st->n_max_ports = 127;
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhub_get_imported_devs(vhub_dev_t * vhub, ioctl_usbip_vhci_imported_dev *idevs, PULONG poutlen)
{
	PAGED_CODE();

	auto idev = idevs;
	unsigned char	n_used_ports = 0;

	auto n_idevs_max = (ULONG)(*poutlen/sizeof(*idevs));
	if (!n_idevs_max) {
		return STATUS_INVALID_PARAMETER;
	}

	TraceCall("Enter");

	ExAcquireFastMutex(&vhub->Mutex);

	for (auto entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {

		if (n_used_ports == n_idevs_max - 1) {
			break;
		}

		auto vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);

		idev->port = char(vpdo->port);
		idev->status = usbip_device_status(2); // SDEV_ST_USED
		idev->vendor = vpdo->vendor;
		idev->product = vpdo->product;
		idev->speed = vpdo->speed;
		idev++;

		++n_used_ports;
	}

	ExReleaseFastMutex(&vhub->Mutex);

	idev->port = -1; /* end of mark */
	return STATUS_SUCCESS;
}
