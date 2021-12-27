#include "vhci_vhub.h"
#include "trace.h"
#include "vhci_vhub.tmh"

#include "vhci_dev.h"
#include "usbip_vhci_api.h"

static PAGEABLE vpdo_dev_t *find_vpdo(vhub_dev_t *vhub, ULONG port)
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

PAGEABLE void vhub_attach_vpdo(vhub_dev_t * vhub, vpdo_dev_t * vpdo)
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

PAGEABLE void vhub_detach_vpdo(vhub_dev_t * vhub, vpdo_dev_t * vpdo)
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

PAGEABLE NTSTATUS vhub_get_information_ex(vhub_dev_t * vhub, PUSB_HUB_INFORMATION_EX pinfo)
{
	PAGED_CODE();

	pinfo->HubType = UsbRootHub;
	pinfo->HighestPortNumber = (USHORT)vhub->n_max_ports;

	vhub_get_hub_descriptor(vhub, &pinfo->u.UsbHubDescriptor);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhub_get_capabilities_ex(vhub_dev_t * vhub, PUSB_HUB_CAPABILITIES_EX pinfo)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vhub);

	pinfo->CapabilityFlags.ul = 0xffffffff;
	pinfo->CapabilityFlags.HubIsHighSpeedCapable = FALSE;
	pinfo->CapabilityFlags.HubIsHighSpeed = FALSE;
	pinfo->CapabilityFlags.HubIsMultiTtCapable = TRUE;
	pinfo->CapabilityFlags.HubIsMultiTt = TRUE;
	pinfo->CapabilityFlags.HubIsRoot = TRUE;
	pinfo->CapabilityFlags.HubIsBusPowered = FALSE;

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhub_get_port_connector_properties(vhub_dev_t * vhub, PUSB_PORT_CONNECTOR_PROPERTIES pinfo, PULONG poutlen)
{
	PAGED_CODE();

	if (pinfo->ConnectionIndex > vhub->n_max_ports)
		return STATUS_INVALID_PARAMETER;
	if (*poutlen < sizeof(USB_PORT_CONNECTOR_PROPERTIES)) {
		*poutlen = sizeof(USB_PORT_CONNECTOR_PROPERTIES);
		return STATUS_BUFFER_TOO_SMALL;
	}

	pinfo->ActualLength = sizeof(USB_PORT_CONNECTOR_PROPERTIES);
	pinfo->UsbPortProperties.ul = 0xffffffff;
	pinfo->UsbPortProperties.PortIsUserConnectable = TRUE;
	pinfo->UsbPortProperties.PortIsDebugCapable = TRUE;
	pinfo->UsbPortProperties.PortHasMultipleCompanions = FALSE;
	pinfo->UsbPortProperties.PortConnectorIsTypeC = FALSE;
	pinfo->CompanionIndex = 0;
	pinfo->CompanionPortNumber = 0;
	pinfo->CompanionHubSymbolicLinkName[0] = L'\0';

	*poutlen = sizeof(USB_PORT_CONNECTOR_PROPERTIES);

	return STATUS_SUCCESS;
}

static PAGEABLE void mark_unplugged_vpdo(vhub_dev_t * vhub, vpdo_dev_t * vpdo)
{
	PAGED_CODE();

	if (vpdo->plugged) {
		vpdo->plugged = FALSE;
		NT_ASSERT(vhub->n_vpdos_plugged > 0);
		vhub->n_vpdos_plugged--;

		IoInvalidateDeviceRelations(vhub->common.pdo, BusRelations);

		Trace(TRACE_LEVEL_INFORMATION, "the device is marked as unplugged: port: %u", vpdo->port);
	} else {
		Trace(TRACE_LEVEL_ERROR, "vpdo already unplugged: port: %u", vpdo->port);
	}
}

PAGEABLE void vhub_mark_unplugged_vpdo(vhub_dev_t * vhub, vpdo_dev_t * vpdo)
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

	for (LIST_ENTRY *entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		vpdo_dev_t *	vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);
		mark_unplugged_vpdo(vhub, vpdo);
	}

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE NTSTATUS vhub_get_ports_status(vhub_dev_t * vhub, ioctl_usbip_vhci_get_ports_status *st)
{
	PAGED_CODE();

	vpdo_dev_t *	vpdo;
	PLIST_ENTRY	entry;

	Trace(TRACE_LEVEL_INFORMATION, "Enter");

	RtlZeroMemory(st, sizeof(*st));
	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		vpdo = CONTAINING_RECORD (entry, vpdo_dev_t, Link);
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

PAGEABLE NTSTATUS vhub_get_imported_devs(vhub_dev_t * vhub, pioctl_usbip_vhci_imported_dev_t idevs, PULONG poutlen)
{
	PAGED_CODE();

	pioctl_usbip_vhci_imported_dev_t	idev = idevs;
	ULONG	n_idevs_max;
	unsigned char	n_used_ports = 0;
	PLIST_ENTRY	entry;

	n_idevs_max = (ULONG)(*poutlen / sizeof(ioctl_usbip_vhci_imported_dev));
	if (n_idevs_max == 0)
		return STATUS_INVALID_PARAMETER;

	Trace(TRACE_LEVEL_INFORMATION, "Enter");

	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {

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
