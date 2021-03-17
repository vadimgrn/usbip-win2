#include "vhci_driver.h"
#include "vhci_plugout.tmh"

#include "usbip_vhci_api.h"

static VOID
abort_pending_req_read(pctx_vusb_t vusb)
{
	WDFREQUEST	req_read_pending;

	WdfSpinLockAcquire(vusb->spin_lock);
	req_read_pending = vusb->pending_req_read;
	vusb->pending_req_read = NULL;
	WdfSpinLockRelease(vusb->spin_lock);

	if (req_read_pending != NULL) {
		TRD(PLUGIN, "abort read request");
		WdfRequestUnmarkCancelable(req_read_pending);
		WdfRequestComplete(req_read_pending, STATUS_DEVICE_NOT_CONNECTED);
	}
}

static VOID
abort_pending_urbr(purb_req_t urbr)
{
	TRD(PLUGIN, "abort pending urbr: %!URBR!", urbr);
	complete_urbr(urbr, STATUS_DEVICE_NOT_CONNECTED);
}

static VOID
abort_all_pending_urbrs(pctx_vusb_t vusb)
{
	WdfSpinLockAcquire(vusb->spin_lock);

	while (!IsListEmpty(&vusb->head_urbr)) {
		purb_req_t	urbr;

		urbr = CONTAINING_RECORD(vusb->head_urbr.Flink, urb_req_t, list_all);
		RemoveEntryListInit(&urbr->list_all);
		RemoveEntryListInit(&urbr->list_state);
		if (!unmark_cancelable_urbr(urbr))
			continue;
		WdfSpinLockRelease(vusb->spin_lock);

		abort_pending_urbr(urbr);

		WdfSpinLockAcquire(vusb->spin_lock);
	}

	WdfSpinLockRelease(vusb->spin_lock);
}

static void
vusb_plugout(pctx_vusb_t vusb)
{
	/*
	 * invalidate first to prevent requests from an upper layer.
	 * If requests are consistently fed into a vusb about to be plugged out,
	 * a live deadlock may occur where vusb aborts pending urbs indefinately.
	 */
	vusb->invalid = TRUE;
	abort_pending_req_read(vusb);
	abort_all_pending_urbrs(vusb);

	TRD(PLUGIN, "plugged out: port: %d", vusb->port);
}

static NTSTATUS
plugout_all_vusbs(pctx_vhci_t vhci)
{
	ULONG	i;

	TRD(PLUGIN, "plugging out all the devices!");

	for (i = 0; i < vhci->n_max_ports; i++) {
		pctx_vusb_t	vusb;

		vusb = get_vusb(vhci, i);
		if (vusb == NULL)
			continue;

		vusb_plugout(vusb);
		put_vusb(vusb);
	}

	return STATUS_SUCCESS;
}

NTSTATUS
plugout_vusb(pctx_vhci_t vhci, CHAR port)
{
	pctx_vusb_t	vusb;

	if (port < 0)
		return plugout_all_vusbs(vhci);

	TRD(IOCTL, "plugging out device: port: %u", port);

	vusb = get_vusb(vhci, port);
	if (vusb == NULL) {
		TRD(PLUGIN, "no matching vusb: port: %u", port);
		return STATUS_NO_SUCH_DEVICE;
	}

	vusb_plugout(vusb);
	put_vusb(vusb);

	TRD(IOCTL, "completed to plug out: port: %u", port);

	return STATUS_SUCCESS;
}
