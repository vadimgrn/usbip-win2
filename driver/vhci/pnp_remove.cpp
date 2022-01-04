#include "pnp_remove.h"
#include "trace.h"
#include "pnp_remove.tmh"

#include "vhci.h"
#include "pnp.h"
#include "irp.h"
#include "wmi.h"
#include "vhub.h"
#include "usbip_vhci_api.h"
#include "usbreq.h"
#include "strutil.h"

namespace
{

/*
* Code must be in nonpaged section if it acquires spinlock.
*/
void complete_pending_read_irp(vpdo_dev_t * vpdo)
{
	KIRQL	oldirql;
	PIRP	irp;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	irp = vpdo->pending_read_irp;
	vpdo->pending_read_irp = nullptr;
	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	if (irp != nullptr) {
		Trace(TRACE_LEVEL_INFORMATION, "complete pending read irp: %p", irp);

		// We got pending_read_irp before submit_urbr
		BOOLEAN valid_irp;
		IoAcquireCancelSpinLock(&oldirql);
		valid_irp = IoSetCancelRoutine(irp, nullptr) != nullptr;
		IoReleaseCancelSpinLock(oldirql);
		if (valid_irp) {
			irp->IoStatus.Information = 0;
			irp_done(irp, STATUS_DEVICE_NOT_CONNECTED);
		}
	}
}

/*
* Code must be in nonpaged section if it acquires spinlock.
*/
void complete_pending_irp(vpdo_dev_t *vpdo)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	while (!IsListEmpty(&vpdo->head_urbr)) {

		auto urbr = CONTAINING_RECORD(vpdo->head_urbr.Flink, struct urb_req, list_all);
		Trace(TRACE_LEVEL_VERBOSE, "Complete pending urbr, seqnum %lu", urbr->seqnum);

		RemoveEntryListInit(&urbr->list_all);
		RemoveEntryListInit(&urbr->list_state);
		/* FIMXE event */
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		auto irp = urbr->irp;
		free_urbr(urbr);
		if (irp) {
			// urbr irps have cancel routine
			IoAcquireCancelSpinLock(&oldirql);
			bool valid_irp = IoSetCancelRoutine(irp, nullptr);
			IoReleaseCancelSpinLock(oldirql);
			if (valid_irp) {
				irp->IoStatus.Information = 0;
				irp_done(irp, STATUS_DEVICE_NOT_CONNECTED);
			}
		}

		KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	}

	vpdo->urbr_sent_partial = nullptr; // sure?
	vpdo->len_sent_partial = 0;
	InitializeListHead(&vpdo->head_urbr_sent);
	InitializeListHead(&vpdo->head_urbr_pending);

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
}

PAGEABLE void invalidate_vhci(vhci_dev_t * vhci)
{
	PAGED_CODE();

	TraceCall("%p", vhci);

	IoSetDeviceInterfaceState(&vhci->DevIntfVhci, FALSE);
	IoSetDeviceInterfaceState(&vhci->DevIntfUSBHC, FALSE);
	RtlFreeUnicodeString(&vhci->DevIntfVhci);

	// Inform WMI to remove this DeviceObject from its list of providers.
	dereg_wmi(vhci);

	Trace(TRACE_LEVEL_INFORMATION, "invalidating vhci device object: %p", to_devobj(vhci));
}

PAGEABLE void invalidate_vhub(vhub_dev_t * vhub)
{
	PAGED_CODE();

	IoSetDeviceInterfaceState(&vhub->DevIntfRootHub, FALSE);
	RtlFreeUnicodeString(&vhub->DevIntfRootHub);

	/* At this point, vhub should has no vpdo. With this assumption, there's no need to remove all vpdos */
	Trace(TRACE_LEVEL_INFORMATION, "VHUB has no vpdos: current # of vpdos: %u", vhub->n_vpdos);

	Trace(TRACE_LEVEL_INFORMATION, "invalidating vhub device object: %p", to_devobj(vhub));
}

PAGEABLE void free_strings(vpdo_dev_t &d)
{
	PAGED_CODE();

	PWSTR *v[] { &d.Manufacturer, &d.Product, &d.SerialNumber };

	for (auto ptr: v) {
		ExFreePoolWithTag(*ptr, USBIP_VHCI_POOL_TAG);
		*ptr = nullptr;
	}
}

PAGEABLE void invalidate_vpdo(vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	TraceCall("%p", vpdo);

	complete_pending_read_irp(vpdo);
	complete_pending_irp(vpdo);

	vhub_detach_vpdo(vhub_from_vpdo(vpdo), vpdo);

	IoSetDeviceInterfaceState(&vpdo->usb_dev_interface, FALSE);

	free_strings(*vpdo);

	if (vpdo->SerialNumberUser) {
		libdrv_free(vpdo->SerialNumberUser);
		vpdo->SerialNumberUser = nullptr;
	}

	if (vpdo->actconfig) {
		ExFreePoolWithTag(vpdo->actconfig, USBIP_VHCI_POOL_TAG);
		vpdo->actconfig = nullptr;
	}

	if (vpdo->fo) { // FIXME
		vpdo->fo->FsContext = nullptr;
		vpdo->fo = nullptr;
	}
}

PAGEABLE void remove_device(vdev_t *vdev)
{
	PAGED_CODE();

	if (vdev->child_pdo) {
		vdev->child_pdo->parent = nullptr;
		if (vdev->child_pdo->fdo)
			vdev->child_pdo->fdo->parent = nullptr;
	}
	if (vdev->fdo) {
		vdev->fdo->pdo = nullptr;
	}
	if (vdev->pdo && vdev->type != VDEV_ROOT) {
		devobj_to_vdev(vdev->pdo)->fdo = nullptr;
	}

	switch (vdev->type) {
	case VDEV_VHCI:
		invalidate_vhci((vhci_dev_t*)vdev);
		break;
	case VDEV_VHUB:
		invalidate_vhub((vhub_dev_t*)vdev);
		break;
	case VDEV_VPDO:
		invalidate_vpdo((vpdo_dev_t*)vdev);
		break;
	}

	// Detach from the underlying devices.
	if (vdev->devobj_lower) {
		IoDetachDevice(vdev->devobj_lower);
		vdev->devobj_lower = nullptr;
	}

	Trace(TRACE_LEVEL_INFORMATION, "deleting device object(%!vdev_type_t!): %p", vdev->type, vdev->Self);

	IoDeleteDevice(vdev->Self);
}

} // namespace


PAGEABLE NTSTATUS pnp_remove_device(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();

	PDEVICE_OBJECT	devobj_lower;

	if (vdev->DevicePnPState == Deleted) {
		Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!: already removed", vdev->type);
		return irp_done_success(irp);
	}

	devobj_lower = vdev->devobj_lower;

	SET_NEW_PNP_STATE(vdev, Deleted);

	remove_device(vdev);

	if (is_fdo(vdev->type)) {
		irp->IoStatus.Status = STATUS_SUCCESS;
		return irp_pass_down(devobj_lower, irp);
	} else {
		return irp_done_success(irp);
	}
}
