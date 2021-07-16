#include "vhci.h"

#include "vhci_dev.h"
#include "usbip_vhci_api.h"

static PAGEABLE pvpdo_dev_t
find_vpdo(pvhub_dev_t vhub, unsigned port)
{
	PLIST_ENTRY	entry;

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		pvpdo_dev_t	vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);

		if (vpdo->port == port) {
			return vpdo;
		}
	}

	return NULL;
}

PAGEABLE pvpdo_dev_t
vhub_find_vpdo(pvhub_dev_t vhub, unsigned port)
{
	pvpdo_dev_t	vpdo;

	ExAcquireFastMutex(&vhub->Mutex);
	vpdo = find_vpdo(vhub, port);
	if (vpdo)
		vdev_add_ref((pvdev_t)vpdo);
	ExReleaseFastMutex(&vhub->Mutex);

	return vpdo;
}

PAGEABLE CHAR
vhub_get_empty_port(pvhub_dev_t vhub)
{
	CHAR	i;

	ExAcquireFastMutex(&vhub->Mutex);
	for (i = 0; i < (CHAR)vhub->n_max_ports; i++) {
		if (find_vpdo(vhub, i) == NULL) {
			ExReleaseFastMutex(&vhub->Mutex);
			return i;
		}
	}
	ExReleaseFastMutex(&vhub->Mutex);

	return -1;
}

PAGEABLE void
vhub_attach_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo)
{
	ExAcquireFastMutex(&vhub->Mutex);

	InsertTailList(&vhub->head_vpdo, &vpdo->Link);
	vhub->n_vpdos++;
	if (vpdo->plugged)
		vhub->n_vpdos_plugged++;

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE void
vhub_detach_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo)
{
	ExAcquireFastMutex(&vhub->Mutex);

	RemoveEntryList(&vpdo->Link);
	InitializeListHead(&vpdo->Link);
	ASSERT(vhub->n_vpdos > 0);
	vhub->n_vpdos--;

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE void
vhub_get_hub_descriptor(pvhub_dev_t vhub, PUSB_HUB_DESCRIPTOR pdesc)
{
	pdesc->bDescriptorLength = 9;
	pdesc->bDescriptorType = 0x29;
	pdesc->bNumberOfPorts = (UCHAR)vhub->n_max_ports;
	pdesc->wHubCharacteristics = 0;
	pdesc->bPowerOnToPowerGood = 1;
	pdesc->bHubControlCurrent = 1;
}

PAGEABLE NTSTATUS
vhub_get_information_ex(pvhub_dev_t vhub, PUSB_HUB_INFORMATION_EX pinfo)
{
	pinfo->HubType = UsbRootHub;
	pinfo->HighestPortNumber = (USHORT)vhub->n_max_ports;

	vhub_get_hub_descriptor(vhub, &pinfo->u.UsbHubDescriptor);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhub_get_capabilities_ex(pvhub_dev_t vhub, PUSB_HUB_CAPABILITIES_EX pinfo)
{
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

PAGEABLE NTSTATUS
vhub_get_port_connector_properties(pvhub_dev_t vhub, PUSB_PORT_CONNECTOR_PROPERTIES pinfo, PULONG poutlen)
{
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

static PAGEABLE void
mark_unplugged_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo)
{
	if (vpdo->plugged) {
		vpdo->plugged = FALSE;
		ASSERT(vhub->n_vpdos_plugged > 0);
		vhub->n_vpdos_plugged--;

		IoInvalidateDeviceRelations(vhub->common.pdo, BusRelations);

		DBGI(DBG_VPDO, "the device is marked as unplugged: port: %u\n", vpdo->port);
	}
	else {
		DBGE(DBG_VHUB, "vpdo already unplugged: port: %u\n", vpdo->port);
	}
}

PAGEABLE void
vhub_mark_unplugged_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo)
{
	ExAcquireFastMutex(&vhub->Mutex);
	mark_unplugged_vpdo(vhub, vpdo);
	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE void
vhub_mark_unplugged_all_vpdos(pvhub_dev_t vhub)
{
	PLIST_ENTRY	entry;

	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		pvpdo_dev_t	vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);
		mark_unplugged_vpdo(vhub, vpdo);
	}

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE NTSTATUS
vhub_get_ports_status(pvhub_dev_t vhub, ioctl_usbip_vhci_get_ports_status *st)
{
	pvpdo_dev_t	vpdo;
	PLIST_ENTRY	entry;

	PAGED_CODE();

	DBGI(DBG_VHUB, "get ports status\n");

	RtlZeroMemory(st, sizeof(*st));
	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		vpdo = CONTAINING_RECORD (entry, vpdo_dev_t, Link);
		if (vpdo->port >= 127) {
			DBGE(DBG_VHUB, "strange port");
			continue;
		}
		st->port_status[vpdo->port] = 1;
	}
	ExReleaseFastMutex(&vhub->Mutex);

	st->n_max_ports = 127;
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhub_get_imported_devs(pvhub_dev_t vhub, pioctl_usbip_vhci_imported_dev_t idevs, PULONG poutlen)
{
	pioctl_usbip_vhci_imported_dev_t	idev = idevs;
	ULONG	n_idevs_max;
	unsigned char	n_used_ports = 0;
	PLIST_ENTRY	entry;

	PAGED_CODE();

	n_idevs_max = (ULONG)(*poutlen / sizeof(ioctl_usbip_vhci_imported_dev));
	if (n_idevs_max == 0)
		return STATUS_INVALID_PARAMETER;

	DBGI(DBG_VHUB, "get imported devices\n");

	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		pvpdo_dev_t	vpdo;

		if (n_used_ports == n_idevs_max - 1)
			break;
		vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);

		idev->port = (CHAR)(vpdo->port);
		idev->status = 2; /* SDEV_ST_USED */;
		idev->vendor = vpdo->vendor;
		idev->product = vpdo->product;
		idev->speed = vpdo->speed;
		idev++;

		n_used_ports++;
	}

	ExReleaseFastMutex(&vhub->Mutex);

	idev->port = 0xff; /* end of mark */

	return STATUS_SUCCESS;
}
