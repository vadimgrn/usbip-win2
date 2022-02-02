#include "usbreq.h"
#include "trace.h"
#include "usbreq.tmh"

#include "vhci.h"
#include "usbip_proto.h"
#include "read.h"
#include "irp.h"

#include <ntstrsafe.h>

namespace
{

void submit_urbr_unlink(vpdo_dev_t *vpdo, unsigned long seq_num_unlink)
{
	auto urbr_unlink = create_urbr(vpdo, nullptr, seq_num_unlink);
	if (!urbr_unlink) {
		return;
	}

	auto status = submit_urbr(vpdo, urbr_unlink);
	if (NT_ERROR(status)) {
		Trace(TRACE_LEVEL_ERROR, "Failed to submit unlink, seq_num_unlink %lu", seq_num_unlink);
		free_urbr(urbr_unlink);
	}
}

void remove_cancelled_urbr(vpdo_dev_t *vpdo, urb_req *urbr)
{
	KIRQL oldirql;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	RemoveEntryListInit(&urbr->list_state);
	RemoveEntryListInit(&urbr->list_all);

	if (vpdo->urbr_sent_partial == urbr) {
		vpdo->urbr_sent_partial = nullptr;
		vpdo->len_sent_partial = 0;
	}

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	submit_urbr_unlink(vpdo, urbr->seqnum);
	Trace(TRACE_LEVEL_VERBOSE, "Cancelled urb destroyed, seqnum %lu", urbr->seqnum);
	free_urbr(urbr);
}

void cancel_urbr(DEVICE_OBJECT*, IRP *irp)
{
	auto vpdo = (vpdo_dev_t*)irp->Tail.Overlay.DriverContext[0];
	NT_ASSERT(vpdo);

	auto urbr = (urb_req*)irp->Tail.Overlay.DriverContext[1];
	NT_ASSERT(urbr);

	Trace(TRACE_LEVEL_INFORMATION, "Irp will be cancelled, seqnum %lu", urbr->seqnum);

	IoReleaseCancelSpinLock(irp->CancelIrql);

	remove_cancelled_urbr(vpdo, urbr);

	irp->IoStatus.Information = 0;
	irp_done(irp, STATUS_CANCELLED);
}

} // namespace

urb_req *find_sent_urbr(vpdo_dev_t *vpdo, unsigned long seqnum)
{
	urb_req *result{};

	KIRQL oldirql;
	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	for (auto le = vpdo->head_urbr_sent.Flink; le != &vpdo->head_urbr_sent; le = le->Flink) {
		auto urbr = CONTAINING_RECORD(le, urb_req, list_state);
		if (urbr->seqnum == seqnum) {
			RemoveEntryListInit(&urbr->list_all);
			RemoveEntryListInit(&urbr->list_state);
			result = urbr;
			break;
		}
	}

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
	return result;
}

urb_req *find_pending_urbr(vpdo_dev_t *vpdo)
{
	if (IsListEmpty(&vpdo->head_urbr_pending)) {
		return nullptr;
	}

	auto urbr = CONTAINING_RECORD(vpdo->head_urbr_pending.Flink, urb_req, list_state);

	urbr->seqnum = next_seqnum(*vpdo);
	RemoveEntryListInit(&urbr->list_state);

	return urbr;
}

urb_req *create_urbr(vpdo_dev_t *vpdo, IRP *irp, unsigned long seqnum_unlink)
{
	auto urbr = (urb_req*)ExAllocateFromNPagedLookasideList(&g_lookaside);
	if (!urbr) {
		Trace(TRACE_LEVEL_ERROR, "Out of memory");
		return nullptr;
	}

	RtlZeroMemory(urbr, sizeof(*urbr));

	urbr->vpdo = vpdo;
	urbr->irp = irp;

	if (irp) {
		irp->Tail.Overlay.DriverContext[0] = vpdo;
		irp->Tail.Overlay.DriverContext[1] = urbr;
	}

	urbr->seqnum_unlink = seqnum_unlink;

	InitializeListHead(&urbr->list_all);
	InitializeListHead(&urbr->list_state);

	return urbr;
}

void free_urbr(urb_req *urbr)
{
	NT_ASSERT(IsListEmpty(&urbr->list_all));
	NT_ASSERT(IsListEmpty(&urbr->list_state)); // FIXME: SYSTEM_SERVICE_EXCEPTION

	ExFreeToNPagedLookasideList(&g_lookaside, urbr);
}

bool is_port_urbr(IRP *irp, USBD_PIPE_HANDLE handle)
{
	if (!(irp && handle)) {
		return false;
	}

	auto urb = (URB*)URB_FROM_IRP(irp);
	if (!urb) {
		return false;
	}

	USBD_PIPE_HANDLE hPipe = 0;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_CONTROL_TRANSFER:
		hPipe = urb->UrbControlTransfer.PipeHandle; // nullptr if (TransferFlags & USBD_DEFAULT_PIPE_TRANSFER)
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		hPipe = urb->UrbControlTransferEx.PipeHandle; // nullptr if (TransferFlags & USBD_DEFAULT_PIPE_TRANSFER)
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		hPipe = urb->UrbBulkOrInterruptTransfer.PipeHandle;
		NT_ASSERT(hPipe);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		hPipe = urb->UrbIsochronousTransfer.PipeHandle;
		NT_ASSERT(hPipe);
		break;
	}

	return hPipe == handle;
}

NTSTATUS submit_urbr(vpdo_dev_t *vpdo, urb_req *urbr)
{
	KIRQL	oldirql;
	KIRQL	oldirql_cancel;
	PIRP	read_irp;
	NTSTATUS status = STATUS_PENDING;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (vpdo->urbr_sent_partial || !vpdo->pending_read_irp) {
		
		if (urbr->irp) {
			IoAcquireCancelSpinLock(&oldirql_cancel);
			IoSetCancelRoutine(urbr->irp, cancel_urbr);
			IoReleaseCancelSpinLock(oldirql_cancel);

			IoMarkIrpPending(urbr->irp);
		}

		InsertTailList(&vpdo->head_urbr_pending, &urbr->list_state);
		InsertTailList(&vpdo->head_urbr, &urbr->list_all);
		
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		Trace(TRACE_LEVEL_VERBOSE, "STATUS_PENDING");
		return STATUS_PENDING;
	}

	IoAcquireCancelSpinLock(&oldirql_cancel);
	bool valid_irp = IoSetCancelRoutine(vpdo->pending_read_irp, nullptr);
	IoReleaseCancelSpinLock(oldirql_cancel);

	if (!valid_irp) {
		Trace(TRACE_LEVEL_VERBOSE, "Read irp was cancelled");
		status = STATUS_INVALID_PARAMETER;
		vpdo->pending_read_irp = nullptr;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
		return status;
	}

	read_irp = vpdo->pending_read_irp;
	vpdo->urbr_sent_partial = urbr;

	urbr->seqnum = next_seqnum(*vpdo);

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	status = store_urbr(read_irp, urbr);

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (status == STATUS_SUCCESS) {
		if (urbr->irp) {
			IoAcquireCancelSpinLock(&oldirql_cancel);
			IoSetCancelRoutine(urbr->irp, cancel_urbr);
			IoReleaseCancelSpinLock(oldirql_cancel);
			IoMarkIrpPending(urbr->irp);
		}

		if (!vpdo->len_sent_partial) {
			vpdo->urbr_sent_partial = nullptr;
			InsertTailList(&vpdo->head_urbr_sent, &urbr->list_state);
		}

		InsertTailList(&vpdo->head_urbr, &urbr->list_all);

		vpdo->pending_read_irp = nullptr;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		irp_done_success(read_irp);
		status = STATUS_PENDING;
	} else {
		vpdo->urbr_sent_partial = nullptr;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		status = STATUS_INVALID_PARAMETER;
	}

	TraceCall("%!STATUS!", status);
	return status;
}
