#include "pnp_remove.h"
#include "trace.h"
#include "pnp_remove.tmh"

#include "vhci.h"
#include "pnp.h"
#include "irp.h"
#include "wmi.h"
#include "vhub.h"
#include "usbip_vhci_api.h"
#include "strutil.h"

namespace
{

PAGEABLE void destroy_vhci(vhci_dev_t *vhci)
{
	PAGED_CODE();

	TraceCall("%p", vhci);

	IoSetDeviceInterfaceState(&vhci->DevIntfVhci, FALSE);
	IoSetDeviceInterfaceState(&vhci->DevIntfUSBHC, FALSE);
	RtlFreeUnicodeString(&vhci->DevIntfVhci);

	// Inform WMI to remove this DeviceObject from its list of providers.
	dereg_wmi(vhci);

	Trace(TRACE_LEVEL_INFORMATION, "Invalidating vhci device object: %p", vhci->Self);
}

PAGEABLE void destroy_vhub(vhub_dev_t *vhub)
{
	PAGED_CODE();

	TraceCall("%p", vhub);

	IoSetDeviceInterfaceState(&vhub->DevIntfRootHub, FALSE);
	RtlFreeUnicodeString(&vhub->DevIntfRootHub);

	// At this point, vhub should has no vpdo. With this assumption, there's no need to remove all vpdos.
	for (int i = 0; i < vhub->NUM_PORTS; ++i) {
		if (auto p = vhub->vpdo[i]) {
			Trace(TRACE_LEVEL_ERROR, "Port[%d] is acquired", i);
		}
	}
}

PAGEABLE void free_strings(vpdo_dev_t &d)
{
	PAGED_CODE();

	PWSTR *v[] { &d.Manufacturer, &d.Product, &d.SerialNumber };

	for (auto i: v) {
		if (auto &ptr = *i) {
			ExFreePoolWithTag(ptr, USBIP_VHCI_POOL_TAG);
			ptr = nullptr;
		}
	}
}

PAGEABLE void free_usb_dev_interface(UNICODE_STRING *iface)
{
	PAGED_CODE();
	
	if (iface->Buffer) {
		if (auto err = IoSetDeviceInterfaceState(iface, FALSE)) {
			Trace(TRACE_LEVEL_ERROR, "IoSetDeviceInterfaceState %!STATUS!", err);
		}

		RtlFreeUnicodeString(iface);
		iface->Buffer = nullptr;
	}

	iface->Length = 0;
	iface->MaximumLength = 0;
}

PAGEABLE void cancel_pending_irps(vpdo_dev_t *vpdo)
{
	PAGED_CODE();
	TraceCall("%p", vpdo);

	IO_CSQ* v[] { &vpdo->read_irp_csq, &vpdo->tx_irps_csq, &vpdo->rx_irps_csq };

	for (auto csq: v) {
		while (auto irp = IoCsqRemoveNextIrp(csq, nullptr)) {
			complete_canceled_irp(vpdo, irp);
		}
	}
}

PAGEABLE void complete_unlinked_irps(vpdo_dev_t *vpdo)
{
	PAGED_CODE();
	TraceCall("%p", vpdo);

	while (auto irp = dequeue_tx_unlink_irp(vpdo)) {
		complete_canceled_irp(vpdo, irp);
	}

	while (auto irp = dequeue_rx_unlink_irp(vpdo)) {
		complete_canceled_irp(vpdo, irp);
	}
}

PAGEABLE void destroy_vpdo(vpdo_dev_t *vpdo)
{
	PAGED_CODE();
	TraceCall("%p, port %d", vpdo, vpdo->port);

	cancel_pending_irps(vpdo);
	complete_unlinked_irps(vpdo);

	vhub_detach_vpdo(vpdo);

	free_usb_dev_interface(&vpdo->usb_dev_interface);
	free_strings(*vpdo);

	if (vpdo->SerialNumberUser) {
		libdrv_free(vpdo->SerialNumberUser);
		vpdo->SerialNumberUser = nullptr;
	}

	if (vpdo->actconfig) {
		ExFreePoolWithTag(vpdo->actconfig, USBIP_VHCI_POOL_TAG);
		vpdo->actconfig = nullptr;
	}

	if (auto fo = (FILE_OBJECT*)InterlockedExchangePointer((PVOID*)&vpdo->fo, nullptr)) {
		auto ptr = (vpdo_dev_t*)InterlockedCompareExchangePointer(&fo->FsContext, nullptr, vpdo);
		if (ptr != vpdo) {
			Trace(TRACE_LEVEL_WARNING, "FsContext(%p) != this(%p)", ptr, vpdo);
		}
	}
}

} // namespace


PAGEABLE void destroy_device(vdev_t *vdev)
{
	PAGED_CODE();

	TraceCall("%!vdev_type_t! %p", vdev->type, vdev);

	if (vdev->child_pdo) {
		vdev->child_pdo->parent = nullptr;
		if (vdev->child_pdo->fdo) {
			vdev->child_pdo->fdo->parent = nullptr;
		}
	}

	if (vdev->fdo) {
		vdev->fdo->pdo = nullptr;
	}

	if (vdev->pdo && vdev->type != VDEV_ROOT) {
		to_vdev(vdev->pdo)->fdo = nullptr;
	}

	switch (vdev->type) {
	case VDEV_VHCI:
		destroy_vhci((vhci_dev_t*)vdev);
		break;
	case VDEV_VHUB:
		destroy_vhub((vhub_dev_t*)vdev);
		break;
	case VDEV_VPDO:
		destroy_vpdo((vpdo_dev_t*)vdev);
		break;
	}

	if (vdev->devobj_lower) { // detach from the underlying devices
		IoDetachDevice(vdev->devobj_lower);
		vdev->devobj_lower = nullptr;
	}

	IoDeleteDevice(vdev->Self);
}

PAGEABLE NTSTATUS pnp_remove_device(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	TraceCall("%p", vdev);

	if (vdev->PnPState == pnp_state::Removed) {
		Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!: already removed", vdev->type);
		return CompleteRequest(irp);
	}

	auto devobj_lower = vdev->devobj_lower;

	set_state(*vdev, pnp_state::Removed);
	destroy_device(vdev);

	if (is_fdo(vdev->type)) {
		irp->IoStatus.Status = STATUS_SUCCESS;
		return irp_pass_down(devobj_lower, irp);
	} else {
		return CompleteRequest(irp);
	}
}
