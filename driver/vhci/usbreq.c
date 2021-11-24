#include "usbreq.h"
#include "trace.h"
#include "usbreq.tmh"

#include "vhci.h"
#include "usbip_proto.h"
#include "vhci_read.h"

const char *dbg_urbr(char* buf, unsigned int len, const struct urb_req *urbr)
{
	if (!urbr) {
		return "[null]";
	}
	
	NTSTATUS st = RtlStringCbPrintfA(buf, len, "[seq:%lu]", urbr->seq_num);
	return st == STATUS_SUCCESS ? buf : "dbg_urbr error";
}

USB_DEFAULT_PIPE_SETUP_PACKET *init_setup_packet(struct usbip_header *hdr, UCHAR dir, UCHAR type, UCHAR recip, UCHAR request)
{
	USB_DEFAULT_PIPE_SETUP_PACKET *setup = get_submit_setup(hdr);

	setup->bmRequestType.Dir = dir;
	setup->bmRequestType.Type = type;
	setup->bmRequestType.Recipient = recip;

	setup->bRequest = request;
	return setup;
}

struct urb_req *find_sent_urbr(vpdo_dev_t *vpdo, struct usbip_header *hdr)
{
	struct urb_req *result = NULL;

	KIRQL oldirql;
	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	for (LIST_ENTRY *le = vpdo->head_urbr_sent.Flink; le != &vpdo->head_urbr_sent; le = le->Flink) {
		struct urb_req *urbr = CONTAINING_RECORD(le, struct urb_req, list_state);
		if (urbr->seq_num == hdr->base.seqnum) {
			RemoveEntryListInit(&urbr->list_all);
			RemoveEntryListInit(&urbr->list_state);
			result = urbr;
			break;
		}
	}

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
	return result;
}

struct urb_req *find_pending_urbr(vpdo_dev_t *vpdo)
{
	if (IsListEmpty(&vpdo->head_urbr_pending)) {
		return NULL;
	}

	struct urb_req *urbr = CONTAINING_RECORD(vpdo->head_urbr_pending.Flink, struct urb_req, list_state);

	urbr->seq_num = ++vpdo->seq_num;
	RemoveEntryListInit(&urbr->list_state);

	return urbr;
}

static void submit_urbr_unlink(vpdo_dev_t *vpdo, unsigned long seq_num_unlink)
{
	struct urb_req *urbr_unlink = create_urbr(vpdo, NULL, seq_num_unlink);
	if (urbr_unlink) {
		NTSTATUS status = submit_urbr(vpdo, urbr_unlink);
		if (NT_ERROR(status)) {
			char buf[DBG_URBR_BUFSZ];
			TraceError(TRACE_GENERAL, "failed to submit unlink urb: %s", dbg_urbr(buf, sizeof(buf), urbr_unlink));
			free_urbr(urbr_unlink);
		}
	}
}

static void remove_cancelled_urbr(pvpdo_dev_t vpdo, struct urb_req *urbr)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	RemoveEntryListInit(&urbr->list_state);
	RemoveEntryListInit(&urbr->list_all);
	if (vpdo->urbr_sent_partial == urbr) {
		vpdo->urbr_sent_partial = NULL;
		vpdo->len_sent_partial = 0;
	}

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	submit_urbr_unlink(vpdo, urbr->seq_num);

	char buf[DBG_URBR_BUFSZ];
	TraceInfo(TRACE_GENERAL, "cancelled urb destroyed: %s", dbg_urbr(buf, sizeof(buf), urbr));
	
	free_urbr(urbr);
}

static void
cancel_urbr(PDEVICE_OBJECT devobj, PIRP irp)
{
	UNREFERENCED_PARAMETER(devobj);

	pvpdo_dev_t	vpdo;
	struct urb_req	*urbr;

	vpdo = (pvpdo_dev_t)irp->Tail.Overlay.DriverContext[0];
	urbr = (struct urb_req *)irp->Tail.Overlay.DriverContext[1];

	vpdo = (pvpdo_dev_t)devobj->DeviceExtension;
	
	{
		char buf[DBG_URBR_BUFSZ];
		TraceInfo(TRACE_GENERAL, "irp will be cancelled: %s", dbg_urbr(buf, sizeof(buf), urbr));
	}

	IoReleaseCancelSpinLock(irp->CancelIrql);

	remove_cancelled_urbr(vpdo, urbr);

	irp->IoStatus.Status = STATUS_CANCELLED;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
}

struct urb_req *
create_urbr(pvpdo_dev_t vpdo, PIRP irp, unsigned long seq_num_unlink)
{
	struct urb_req	*urbr;

	urbr = ExAllocateFromNPagedLookasideList(&g_lookaside);
	if (urbr == NULL) {
		TraceError(TRACE_URB, "out of memory");
		return NULL;
	}
	RtlZeroMemory(urbr, sizeof(*urbr));
	urbr->vpdo = vpdo;
	urbr->irp = irp;
	if (irp != NULL) {
		irp->Tail.Overlay.DriverContext[0] = vpdo;
		irp->Tail.Overlay.DriverContext[1] = urbr;
	}

	urbr->seq_num_unlink = seq_num_unlink;
	InitializeListHead(&urbr->list_all);
	InitializeListHead(&urbr->list_state);
	return urbr;
}

void
free_urbr(struct urb_req *urbr)
{
	NT_ASSERT(IsListEmpty(&urbr->list_all));
	NT_ASSERT(IsListEmpty(&urbr->list_state));
	ExFreeToNPagedLookasideList(&g_lookaside, urbr);
}

BOOLEAN is_port_urbr(struct urb_req *urbr, USBD_PIPE_HANDLE handle)
{
	IRP *irp = urbr->irp;
	if (!irp) {
		return FALSE;
	}

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	URB *urb = irpstack->Parameters.Others.Argument1;
	if (!urb) {
		return FALSE;
	}

	USBD_PIPE_HANDLE hPipe = 0;

	switch (urb->UrbHeader.Function) {
/*
	case URB_FUNCTION_CONTROL_TRANSFER:
		hPipe = urb->UrbControlTransfer.PipeHandle;
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		hPipe = urb->UrbControlTransferEx.PipeHandle;
		break;
*/
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		hPipe = urb->UrbBulkOrInterruptTransfer.PipeHandle;
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		hPipe = urb->UrbIsochronousTransfer.PipeHandle;
		break;
	default:
		return FALSE;
	}

	NT_ASSERT(hPipe);
	return hPipe == handle;
}

NTSTATUS submit_urbr(vpdo_dev_t *vpdo, struct urb_req *urbr)
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

		TraceInfo(TRACE_URB, "STATUS_PENDING");
		return STATUS_PENDING;
	}

	IoAcquireCancelSpinLock(&oldirql_cancel);
	bool valid_irp = IoSetCancelRoutine(vpdo->pending_read_irp, NULL);
	IoReleaseCancelSpinLock(oldirql_cancel);

	if (!valid_irp) {
		TraceInfo(TRACE_URB, "Read irp was cancelled");
		status = STATUS_INVALID_PARAMETER;
		vpdo->pending_read_irp = NULL;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
		return status;
	}

	read_irp = vpdo->pending_read_irp;
	vpdo->urbr_sent_partial = urbr;

	urbr->seq_num = ++vpdo->seq_num;

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
			vpdo->urbr_sent_partial = NULL;
			InsertTailList(&vpdo->head_urbr_sent, &urbr->list_state);
		}

		InsertTailList(&vpdo->head_urbr, &urbr->list_all);

		vpdo->pending_read_irp = NULL;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		read_irp->IoStatus.Status = STATUS_SUCCESS;
		IoCompleteRequest(read_irp, IO_NO_INCREMENT);
		status = STATUS_PENDING;
	} else {
		vpdo->urbr_sent_partial = NULL;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		status = STATUS_INVALID_PARAMETER;
	}

	TraceInfo(TRACE_URB, "%!STATUS!", status);
	return status;
}
