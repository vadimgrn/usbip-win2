#include "vhub.h"
#include "trace.h"
#include "vhub.tmh"

#include "dev.h"
#include "usbip_vhci_api.h"
#include "usbdsc.h"
#include "ch11.h"

#include <intrin.h>

PAGEABLE vpdo_dev_t *vhub_find_vpdo(vhub_dev_t *vhub, int port)
{
	PAGED_CODE();

	if (!is_valid_port(port)) {
		return nullptr;
	}

	ExAcquireFastMutex(&vhub->Mutex);

	auto vpdo = vhub->vpdo[port - 1];
	if (vpdo) {
		NT_ASSERT(vpdo->port == port);
	}

	ExReleaseFastMutex(&vhub->Mutex);
	return vpdo;
}

PAGEABLE bool vhub_attach_vpdo(vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	NT_ASSERT(!vpdo->port);
	auto vhub = vhub_from_vpdo(vpdo);

	ExAcquireFastMutex(&vhub->Mutex);

	for (int i = 0; i < vhub->NUM_PORTS; ++i) {
		auto &ptr = vhub->vpdo[i];
		if (!ptr) {
			ptr = vpdo;
			vpdo->port = i + 1;
			NT_ASSERT(is_valid_port(vpdo->port));
			break;
		}
	}

	ExReleaseFastMutex(&vhub->Mutex);

	TraceCall("%p, port %d", vpdo, vpdo->port);
	return vpdo->port;
}

PAGEABLE void vhub_detach_vpdo(vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	TraceCall("%p, port %d", vpdo, vpdo->port);

	if (!vpdo->port) { // was not attached
		return;
	}

	auto vhub = vhub_from_vpdo(vpdo);

	ExAcquireFastMutex(&vhub->Mutex);
	{
		auto i = vpdo->port - 1;
		NT_ASSERT(vhub->vpdo[i] == vpdo);
		vhub->vpdo[i] = nullptr;
	}
	ExReleaseFastMutex(&vhub->Mutex);

	vpdo->port = 0;
}

PAGEABLE void vhub_get_hub_descriptor(vhub_dev_t *vhub, USB_HUB_DESCRIPTOR &d)
{
	PAGED_CODE();

	d.bDescriptorLength = USB_DT_HUB_NONVAR_SIZE + 2;
	d.bDescriptorType = USB_20_HUB_DESCRIPTOR_TYPE;
	d.bNumberOfPorts = vhub->NUM_PORTS; 
	d.wHubCharacteristics = 0;
	d.bPowerOnToPowerGood = 1;
	d.bHubControlCurrent = 1;
	//d.bRemoveAndPowerMask = 0;
}

PAGEABLE void vhub_get_hub_descriptor(vhub_dev_t *vhub, USB_30_HUB_DESCRIPTOR &d)
{
	PAGED_CODE();

	static_assert(sizeof(d) == USB_DT_SS_HUB_SIZE);

	d.bLength = sizeof(d);
	d.bDescriptorType = USB_30_HUB_DESCRIPTOR_TYPE;
	d.bNumberOfPorts = vhub->NUM_PORTS; 
	d.wHubCharacteristics = 0;
	d.bPowerOnToPowerGood = 0;
	d.bHubControlCurrent = 0;
	d.bHubHdrDecLat = 0; // The hub packet header decode latency
	d.wHubDelay = 0; // The average delay, in nanoseconds, that is introduced by the hub
	d.DeviceRemovable = USHORT(-1 << 1); // Indicates whether a removable device is attached to each port
}

PAGEABLE NTSTATUS vhub_get_information_ex(vhub_dev_t *vhub, USB_HUB_INFORMATION_EX &p)
{
	PAGED_CODE();

	p.HubType = UsbRootHub; // Usb30Hub
	p.HighestPortNumber = vhub->NUM_PORTS;

	vhub_get_hub_descriptor(vhub, p.u.UsbHubDescriptor);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhub_get_capabilities_ex(vhub_dev_t*, USB_HUB_CAPABILITIES_EX &p)
{
	PAGED_CODE();

	auto &f = p.CapabilityFlags;
	f.ul = 0;

//	f.HubIsHighSpeedCapable = false;
//	f.HubIsHighSpeed = false;
	
	f.HubIsMultiTtCapable = true;
	f.HubIsMultiTt = true;
	
	f.HubIsRoot = true;
	//f.HubIsArmedWakeOnConnect = true; // the hub is armed to wake when a device is connected to the hub
//	f.HubIsBusPowered = false;

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhub_get_port_connector_properties(vhub_dev_t*, USB_PORT_CONNECTOR_PROPERTIES *p, ULONG *poutlen)
{
	PAGED_CODE();

	if (!is_valid_port(p->ConnectionIndex)) {
		return STATUS_INVALID_PARAMETER;
	}

	if (*poutlen < sizeof(*p)) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	*poutlen = sizeof(*p);

	p->ActualLength = sizeof(*p);

	p->UsbPortProperties.ul = 0;
	p->UsbPortProperties.PortIsUserConnectable = true;
	p->UsbPortProperties.PortIsDebugCapable = true;
//	p->UsbPortProperties.PortHasMultipleCompanions = FALSE;
//	p->UsbPortProperties.PortConnectorIsTypeC = FALSE;

	p->CompanionIndex = 0;
	p->CompanionPortNumber = 0;
	p->CompanionHubSymbolicLinkName[0] = L'\0';

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhub_unplug_vpdo(vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	NT_ASSERT(vpdo);
	static_assert(sizeof(vpdo->unplugged) == sizeof(CHAR));

	if (InterlockedExchange8(reinterpret_cast<volatile CHAR*>(&vpdo->unplugged), true)) {
		Trace(TRACE_LEVEL_INFORMATION, "Device is already unplugged, port %d", vpdo->port);
		return STATUS_OPERATION_IN_PROGRESS;
	}

	Trace(TRACE_LEVEL_INFORMATION, "Unplugging device %p on port %d", vpdo, vpdo->port);

	auto vhub = vhub_from_vpdo(vpdo);
	IoInvalidateDeviceRelations(vhub->pdo, BusRelations);

	return STATUS_SUCCESS;
}

PAGEABLE void vhub_unplug_all_vpdo(vhub_dev_t *vhub)
{
	PAGED_CODE();

	ExAcquireFastMutex(&vhub->Mutex);

	for (auto i: vhub->vpdo) {
		if (i) {
			vhub_unplug_vpdo(i);
		}
	}

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE NTSTATUS vhub_get_ports_status(vhub_dev_t *vhub, ioctl_usbip_vhci_get_ports_status &st, ULONG *poutlen)
{
	PAGED_CODE();

	if (*poutlen != sizeof(st)) {
		*poutlen = sizeof(st);
		STATUS_INVALID_BUFFER_SIZE;
	}

	st.n_max_ports = vhub->NUM_PORTS;
	NT_ASSERT(st.n_max_ports == vhub->NUM_PORTS);

	int acquired = 0;

	ExAcquireFastMutex(&vhub->Mutex);

	for (int i = 0; i < vhub->NUM_PORTS; ++i) {
		bool busy = vhub->vpdo[i];
		st.port_status[i] = busy;
		acquired += busy;
	}

	ExReleaseFastMutex(&vhub->Mutex);

	TraceCall("Acquired ports %d/%d", acquired, vhub->NUM_PORTS);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhub_get_imported_devs(vhub_dev_t *vhub, ioctl_usbip_vhci_imported_dev *dev, size_t cnt)
{
	PAGED_CODE();

	TraceCall("cnt %Iu", cnt);

	if (!cnt) {
		return STATUS_INVALID_PARAMETER;
	}

	ExAcquireFastMutex(&vhub->Mutex);

	for (auto vpdo: vhub->vpdo) {

		if (!vpdo) {
			continue;
		}

		if (!--cnt) {
			break;
		}

		auto &d = vpdo->descriptor;
		NT_ASSERT(is_valid_dsc(&d));

		dev->port = static_cast<char>(vpdo->port);
		NT_ASSERT(dev->port == vpdo->port);

		dev->status = usbip_device_status(2); // SDEV_ST_USED
		dev->vendor = d.idVendor;
		dev->product = d.idProduct;
		dev->speed = vpdo->speed;

		++dev;
	}

	ExReleaseFastMutex(&vhub->Mutex);

	dev->port = -1; // end of mark
	return STATUS_SUCCESS;
}
