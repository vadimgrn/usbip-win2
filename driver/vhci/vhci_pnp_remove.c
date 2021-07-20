#include "vhci_pnp_remove.h"
#include "vhci.h"
#include "vhci_dbg.h"
#include "vhci_pnp.h"
#include "vhci_irp.h"
#include "vhci_wmi.h"
#include "vhci_vhub.h"
#include "usbip_vhci_api.h"
#include "usbreq.h"

static PAGEABLE void
complete_pending_read_irp(pvpdo_dev_t vpdo)
{
	KIRQL	oldirql;
	PIRP	irp;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	irp = vpdo->pending_read_irp;
	vpdo->pending_read_irp = NULL;
	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	if (irp != NULL) {
		DBGI(DBG_PNP, "complete pending read irp: %p\n", irp);

		// We got pending_read_irp before submit_urbr
		BOOLEAN valid_irp;
		IoAcquireCancelSpinLock(&oldirql);
		valid_irp = IoSetCancelRoutine(irp, NULL) != NULL;
		IoReleaseCancelSpinLock(oldirql);
		if (valid_irp) {
			irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
			irp->IoStatus.Information = 0;
			IoCompleteRequest(irp, IO_NO_INCREMENT);
		}
	}
}

static PAGEABLE void
complete_pending_irp(pvpdo_dev_t vpdo)
{
	KIRQL	oldirql;
	BOOLEAN	valid_irp;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	while (!IsListEmpty(&vpdo->head_urbr)) {
		struct urb_req *urbr;
		PIRP	irp;

		urbr = CONTAINING_RECORD(vpdo->head_urbr.Flink, struct urb_req, list_all);

		DBGI(DBG_PNP, "complete pending urbr: %s\n", dbg_urbr(urbr));

		RemoveEntryListInit(&urbr->list_all);
		RemoveEntryListInit(&urbr->list_state);
		/* FIMXE event */
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		irp = urbr->irp;
		free_urbr(urbr);
		if (irp != NULL) {
			// urbr irps have cancel routine
			IoAcquireCancelSpinLock(&oldirql);
			valid_irp = IoSetCancelRoutine(irp, NULL) != NULL;
			IoReleaseCancelSpinLock(oldirql);
			if (valid_irp) {
				irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
				irp->IoStatus.Information = 0;
				IoCompleteRequest(irp, IO_NO_INCREMENT);
			}
		}

		KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	}

	vpdo->urbr_sent_partial = NULL; // sure?
	vpdo->len_sent_partial = 0;
	InitializeListHead(&vpdo->head_urbr_sent);
	InitializeListHead(&vpdo->head_urbr_pending);

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
}

static PAGEABLE void
invalidate_vhci(pvhci_dev_t vhci)
{
	IoSetDeviceInterfaceState(&vhci->DevIntfVhci, FALSE);
	IoSetDeviceInterfaceState(&vhci->DevIntfUSBHC, FALSE);
	RtlFreeUnicodeString(&vhci->DevIntfVhci);

	// Inform WMI to remove this DeviceObject from its list of providers.
	dereg_wmi(vhci);

	DBGI(DBG_PNP, "invalidating vhci device object: 0x%p\n", TO_DEVOBJ(vhci));
}

static PAGEABLE void
invalidate_vhub(pvhub_dev_t vhub)
{
	IoSetDeviceInterfaceState(&vhub->DevIntfRootHub, FALSE);
	RtlFreeUnicodeString(&vhub->DevIntfRootHub);

	/* At this point, vhub should has no vpdo. With this assumption, there's no need to remove all vpdos */
	DBGI(DBG_PNP, "VHUB has no vpdos: current # of vpdos: %u\n", vhub->n_vpdos);

	DBGI(DBG_PNP, "invalidating vhub device object: 0x%p\n", TO_DEVOBJ(vhub));
}

static PAGEABLE void
invalidate_vpdo(pvpdo_dev_t vpdo)
{
	complete_pending_read_irp(vpdo);
	complete_pending_irp(vpdo);

	vhub_detach_vpdo(VHUB_FROM_VPDO(vpdo), vpdo);

	IoSetDeviceInterfaceState(&vpdo->usb_dev_interface, FALSE);

	if (vpdo->winstid != NULL) {
		ExFreePoolWithTag(vpdo->winstid, USBIP_VHCI_POOL_TAG);
		vpdo->winstid = NULL;
	}

	//FIXME
	if (vpdo->fo) {
		vpdo->fo->FsContext = NULL;
		vpdo->fo = NULL;
	}
}

static PAGEABLE void
remove_device(pvdev_t vdev)
{
	if (vdev->child_pdo != NULL) {
		vdev->child_pdo->parent = NULL;
		if (vdev->child_pdo->fdo)
			vdev->child_pdo->fdo->parent = NULL;
	}
	if (vdev->fdo != NULL) {
		vdev->fdo->pdo = NULL;
	}
	if (vdev->pdo != NULL && vdev->type != VDEV_ROOT) {
		DEVOBJ_TO_VDEV(vdev->pdo)->fdo = NULL;
	}

	switch (vdev->type) {
	case VDEV_VHCI:
		invalidate_vhci((pvhci_dev_t)vdev);
		break;
	case VDEV_VHUB:
		invalidate_vhub((pvhub_dev_t)vdev);
		break;
	case VDEV_VPDO:
		invalidate_vpdo((pvpdo_dev_t)vdev);
		break;
	default:
		break;
	}

	// Detach from the underlying devices.
	if (vdev->devobj_lower != NULL) {
		IoDetachDevice(vdev->devobj_lower);
		vdev->devobj_lower = NULL;
	}

	DBGI(DBG_VDEV, "deleting device object(%s): 0x%p\n", dbg_vdev_type(vdev->type), vdev->Self);

	IoDeleteDevice(vdev->Self);
}

PAGEABLE NTSTATUS
pnp_remove_device(pvdev_t vdev, PIRP irp)
{
	PDEVICE_OBJECT	devobj_lower;

	if (vdev->DevicePnPState == Deleted) {
		DBGI(DBG_PNP, "%s: already removed\n", dbg_vdev_type(vdev->type));
		return irp_success(irp);
	}

	devobj_lower = vdev->devobj_lower;

	SET_NEW_PNP_STATE(vdev, Deleted);

	remove_device(vdev);

	if (IS_FDO((vdev)->type)) {
		irp->IoStatus.Status = STATUS_SUCCESS;
		return irp_pass_down(devobj_lower, irp);
	}
	else
		return irp_success(irp);
}