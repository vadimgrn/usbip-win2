#include "vhub.h"
#include "trace.h"
#include "vhub.tmh"

#include "dev.h"
#include "usbip_vhci_api.h"
#include "usbdsc.h"

#include <intrin.h>

namespace
{

PAGEABLE vpdo_dev_t *find_vpdo(vhub_dev_t *vhub, int port)
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

	if (!vpdo->plugged) {
		Trace(TRACE_LEVEL_INFORMATION, "Device was already marked as unplugged, port %d", vpdo->port);
		return;
	}

	vpdo->plugged = false;

	NT_ASSERT(vhub->n_vpdos_plugged > 0);
	--vhub->n_vpdos_plugged;

	IoInvalidateDeviceRelations(vhub->pdo, BusRelations);
	Trace(TRACE_LEVEL_INFORMATION, "Device is marked as unplugged, port %d", vpdo->port);
}

} // namespace


PAGEABLE vpdo_dev_t *vhub_find_vpdo(vhub_dev_t *vhub, int port)
{
	PAGED_CODE();

	ExAcquireFastMutex(&vhub->Mutex);
	
	auto vpdo = find_vpdo(vhub, port);
	if (vpdo) {
		vdev_add_ref(vpdo);
	}

	ExReleaseFastMutex(&vhub->Mutex);
	return vpdo;
}

PAGEABLE void vhub_attach_vpdo(vhub_dev_t *vhub, vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	TraceCall("%p", vpdo);

	ExAcquireFastMutex(&vhub->Mutex);
	{
		InsertTailList(&vhub->head_vpdo, &vpdo->Link);
		if (vpdo->plugged) {
			++vhub->n_vpdos_plugged;
			NT_ASSERT(vhub->n_vpdos_plugged <= get_vpdo_count(*vhub));
		}
	}
	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE void vhub_detach_vpdo_and_release_port(vhub_dev_t *vhub, vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	TraceCall("%p, port %d", vpdo, vpdo->port);

	ExAcquireFastMutex(&vhub->Mutex);
	{
		RemoveEntryList(&vpdo->Link);
		InitializeListHead(&vpdo->Link);

		[[maybe_unused]] auto err = release_port(*vhub, vpdo->port, false);
		NT_ASSERT(!err);
	}
	ExReleaseFastMutex(&vhub->Mutex);

	vpdo->port = 0;
}

PAGEABLE void vhub_get_hub_descriptor(vhub_dev_t *vhub, USB_HUB_DESCRIPTOR *d)
{
	PAGED_CODE();

	d->bDescriptorLength = 9;
	d->bDescriptorType = USB_20_HUB_DESCRIPTOR_TYPE; // USB_30_HUB_DESCRIPTOR_TYPE
	d->bNumberOfPorts = vhub->NUM_PORTS; 
	d->wHubCharacteristics = 0;
	d->bPowerOnToPowerGood = 1;
	d->bHubControlCurrent = 1;
}

PAGEABLE NTSTATUS vhub_get_information_ex(vhub_dev_t *vhub, USB_HUB_INFORMATION_EX *p)
{
	PAGED_CODE();

	p->HubType = UsbRootHub;
	p->HighestPortNumber = vhub->NUM_PORTS;

	vhub_get_hub_descriptor(vhub, &p->u.UsbHubDescriptor);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhub_get_capabilities_ex(vhub_dev_t*, PUSB_HUB_CAPABILITIES_EX p)
{
	PAGED_CODE();

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

	if (p->ConnectionIndex > vhub->NUM_PORTS) {
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

PAGEABLE void vhub_mark_unplugged_all_vpdos(vhub_dev_t *vhub)
{
	PAGED_CODE();

	ExAcquireFastMutex(&vhub->Mutex);

	for (auto entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		auto vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);
		mark_unplugged_vpdo(vhub, vpdo);
	}

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE NTSTATUS vhub_get_ports_status(vhub_dev_t *vhub, ioctl_usbip_vhci_get_ports_status *st)
{
	PAGED_CODE();

	ExAcquireFastMutex(&vhub->Mutex);
	auto bm_ports = vhub->bm_ports;
	ExReleaseFastMutex(&vhub->Mutex);

	st->n_max_ports = vhub->NUM_PORTS;
	NT_ASSERT(st->n_max_ports == vhub->NUM_PORTS);

	for (int i = 0; i < vhub->NUM_PORTS; ++i) {
		st->port_status[i] = bool(bm_ports & (1 << i));
	}

	TraceCall("bm_ports %#04lx, max ports %d", bm_ports, vhub->NUM_PORTS);
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

	for (auto entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo && --cnt; entry = entry->Flink, ++dev) {

		auto vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);

		auto &d = vpdo->descriptor;
		NT_ASSERT(is_valid_dsc(&d));

		dev->port = static_cast<char>(vpdo->port);
		NT_ASSERT(dev->port == vpdo->port);

		dev->status = usbip_device_status(2); // SDEV_ST_USED
		dev->vendor = d.idVendor;
		dev->product = d.idProduct;
		dev->speed = vpdo->speed;
	}

	ExReleaseFastMutex(&vhub->Mutex);

	dev->port = -1; // end of mark
	return STATUS_SUCCESS;
}

PAGEABLE int acquire_port(vhub_dev_t &vhub)
{
	PAGED_CODE();

	ULONG idx = 0;
	ExAcquireFastMutex(&vhub.Mutex);

	auto mask = ~vhub.bm_ports & vhub.PORTS_MASK;
	auto found = BitScanForward(&idx, mask);

	if (found) {
		[[maybe_unused]] auto was_set = BitTestAndSet(reinterpret_cast<LONG*>(&vhub.bm_ports), idx);
		NT_ASSERT(!was_set);
	}

	ExReleaseFastMutex(&vhub.Mutex);
	
	if (!found) {
		return 0;
	}

	int port = idx + 1;

	NT_ASSERT(port > 0);
	NT_ASSERT(port <= vhub.NUM_PORTS);

	TraceCall("%d", port);
	return port;
}

PAGEABLE NTSTATUS release_port(vhub_dev_t &vhub, int port, bool lock)
{
	PAGED_CODE();

	TraceCall("%d", port);

	if (!(port > 0 && port <= vhub.NUM_PORTS)) {
		return STATUS_INVALID_PARAMETER;
	}

	if (lock) {
		ExAcquireFastMutex(&vhub.Mutex);
	}

	NT_VERIFY(BitTestAndReset(reinterpret_cast<LONG*>(&vhub.bm_ports), port - 1));

	if (lock) {
		ExReleaseFastMutex(&vhub.Mutex);
	}

	return STATUS_SUCCESS;
}

PAGEABLE int get_vpdo_count(const vhub_dev_t &vhub)
{
	PAGED_CODE();

	auto cnt = PopulationCount64(vhub.bm_ports & vhub.PORTS_MASK);
	NT_ASSERT(cnt <= vhub.NUM_PORTS);

	return static_cast<int>(cnt);
}
