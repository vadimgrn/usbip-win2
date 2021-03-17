#include "vhci_driver.h"
#include "vhci_read.tmh"

#include "vhci_urbr.h"

extern NTSTATUS
store_urbr_partial(WDFREQUEST req_read, purb_req_t urbr);

static purb_req_t
find_pending_urbr(pctx_vusb_t vusb)
{
	purb_req_t	urbr;

	if (IsListEmpty(&vusb->head_urbr_pending))
		return NULL;

	urbr = CONTAINING_RECORD(vusb->head_urbr_pending.Flink, urb_req_t, list_state);
	urbr->seq_num = ++(vusb->seq_num);
	RemoveEntryListInit(&urbr->list_state);
	return urbr;
}

static purb_req_t
get_partial_urbr(pctx_vusb_t vusb)
{
	purb_req_t	urbr;

	if (vusb->urbr_sent_partial == NULL)
		return NULL;

	urbr = vusb->urbr_sent_partial;
	if (unmark_cancelable_urbr(urbr))
		return urbr;
	else {
		/* There's on-going cancellation. It's enough to just clear out followings. */
		vusb->urbr_sent_partial = NULL;
		vusb->len_sent_partial = 0;
		return NULL;
	}
}

static VOID
req_read_cancelled(WDFREQUEST req_read)
{
	pctx_vusb_t	vusb;

	TRD(READ, "a pending read req cancelled");

	vusb = get_vusb_by_req(req_read);
	if (vusb != NULL) {
		WdfSpinLockAcquire(vusb->spin_lock);
		if (vusb->pending_req_read == req_read) {
			vusb->pending_req_read = NULL;
		}
		WdfSpinLockRelease(vusb->spin_lock);

		/* put_vusb() at <= DISPATCH sometimes causes BSOD */
		put_vusb_passively(vusb);
	}

	WdfRequestComplete(req_read, STATUS_CANCELLED);
}

static NTSTATUS
read_vusb(pctx_vusb_t vusb, WDFREQUEST req)
{
	purb_req_t	urbr;
	NTSTATUS status;

	TRD(READ, "Enter");

	WdfSpinLockAcquire(vusb->spin_lock);

	if (vusb->pending_req_read) {
		WdfSpinLockRelease(vusb->spin_lock);
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	urbr = get_partial_urbr(vusb);
	if (urbr != NULL) {
		WdfSpinLockRelease(vusb->spin_lock);

		status = store_urbr_partial(req, urbr);

		WdfSpinLockAcquire(vusb->spin_lock);
		vusb->len_sent_partial = 0;
	}
	else {
		urbr = find_pending_urbr(vusb);
		if (urbr == NULL) {
			vusb->pending_req_read = req;

			status = WdfRequestMarkCancelableEx(req, req_read_cancelled);
			if (!NT_SUCCESS(status)) {
				if (vusb->pending_req_read == req) {
					vusb->pending_req_read = NULL;
				}
			}
			WdfSpinLockRelease(vusb->spin_lock);
			if (!NT_SUCCESS(status)) {
				WdfRequestComplete(req, status);
				TRE(READ, "a pending read req cancelled: %!STATUS!", status);
			}

			return STATUS_PENDING;
		}
		vusb->urbr_sent_partial = urbr;
		WdfSpinLockRelease(vusb->spin_lock);

		status = store_urbr(req, urbr);

		WdfSpinLockAcquire(vusb->spin_lock);
	}

	if (status != STATUS_SUCCESS) {
		BOOLEAN	unmarked;
		RemoveEntryListInit(&urbr->list_all);
		unmarked = unmark_cancelable_urbr(urbr);
		vusb->urbr_sent_partial = NULL;
		WdfSpinLockRelease(vusb->spin_lock);

		if (unmarked)
			complete_urbr(urbr, status);
	}
	else {
		if (vusb->len_sent_partial == 0) {
			InsertTailList(&vusb->head_urbr_sent, &urbr->list_state);
			vusb->urbr_sent_partial = NULL;
		}
		WdfSpinLockRelease(vusb->spin_lock);
	}
	return status;
}

VOID
io_read(_In_ WDFQUEUE queue, _In_ WDFREQUEST req, _In_ size_t len)
{
	pctx_vusb_t	vusb;
	NTSTATUS	status;

	UNREFERENCED_PARAMETER(queue);

	TRD(READ, "Enter: len: %u", (ULONG)len);

	vusb = get_vusb_by_req(req);
	if (vusb == NULL) {
		TRD(READ, "vusb disconnected: port: %u", TO_SAFE_VUSB_FROM_REQ(req)->port);
		status = STATUS_DEVICE_NOT_CONNECTED;
	}
	else {
		status = read_vusb(vusb, req);
		put_vusb(vusb);
	}

	if (status != STATUS_PENDING) {
		WdfRequestComplete(req, status);
	}

	TRD(READ, "Leave: %!STATUS!", status);
}
