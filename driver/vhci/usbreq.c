#include "usbreq.h"
#include "vhci.h"
#include "usbip_proto.h"
#include "vhci_read.h"

#ifdef DBG

const char *
dbg_urbr(struct urb_req *urbr)
{
	static char	buf[128];

	if (urbr == NULL)
		return "[null]";
	libdrv_snprintf(buf, 128, "[seq:%u]", urbr->seq_num);
	return buf;
}

#endif

void build_setup_packet(USB_DEFAULT_PIPE_SETUP_PACKET *setup, UCHAR dir, UCHAR type, UCHAR recip, UCHAR request)
{
	setup->bmRequestType.B = 0; // clear reserved bits as well

	setup->bmRequestType.Dir = dir;
	setup->bmRequestType.Type = type;
	setup->bmRequestType.Recipient = recip;

	setup->bRequest = request;
}

struct urb_req *
find_sent_urbr(pvpdo_dev_t vpdo, struct usbip_header *hdr)
{
	KIRQL		oldirql;
	PLIST_ENTRY	le;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	for (le = vpdo->head_urbr_sent.Flink; le != &vpdo->head_urbr_sent; le = le->Flink) {
		struct urb_req	*urbr;
		urbr = CONTAINING_RECORD(le, struct urb_req, list_state);
		if (urbr->seq_num == hdr->base.seqnum) {
			RemoveEntryListInit(&urbr->list_all);
			RemoveEntryListInit(&urbr->list_state);
			KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
			return urbr;
		}
	}
	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	return NULL;
}

struct urb_req *
find_pending_urbr(pvpdo_dev_t vpdo)
{
	struct urb_req	*urbr;

	if (IsListEmpty(&vpdo->head_urbr_pending))
		return NULL;

	urbr = CONTAINING_RECORD(vpdo->head_urbr_pending.Flink, struct urb_req, list_state);
	urbr->seq_num = ++(vpdo->seq_num);
	RemoveEntryListInit(&urbr->list_state);
	return urbr;
}

static void
submit_urbr_unlink(pvpdo_dev_t vpdo, unsigned long seq_num_unlink)
{
	struct urb_req	*urbr_unlink;

	urbr_unlink = create_urbr(vpdo, NULL, seq_num_unlink);
	if (urbr_unlink != NULL) {
		NTSTATUS	status = submit_urbr(vpdo, urbr_unlink);
		if (NT_ERROR(status)) {
			DBGI(DBG_GENERAL, "failed to submit unlink urb: %s\n", dbg_urbr(urbr_unlink));
			free_urbr(urbr_unlink);
		}
	}
}

static void
remove_cancelled_urbr(pvpdo_dev_t vpdo, struct urb_req *urbr)
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

	DBGI(DBG_GENERAL, "cancelled urb destroyed: %s\n", dbg_urbr(urbr));
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
	DBGI(DBG_GENERAL, "irp will be cancelled: %s\n", dbg_urbr(urbr));
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
		DBGE(DBG_URB, "create_urbr: out of memory\n");
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
	ASSERT(IsListEmpty(&urbr->list_all));
	ASSERT(IsListEmpty(&urbr->list_state));
	ExFreeToNPagedLookasideList(&g_lookaside, urbr);
}

BOOLEAN
is_port_urbr(struct urb_req *urbr, unsigned char epaddr)
{
	PIRP	irp = urbr->irp;
	PURB	urb;
	PIO_STACK_LOCATION	irpstack;
	USBD_PIPE_HANDLE	hPipe;

	if (irp == NULL)
		return FALSE;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	urb = irpstack->Parameters.Others.Argument1;
	if (urb == NULL)
		return FALSE;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		hPipe = urb->UrbBulkOrInterruptTransfer.PipeHandle;
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		hPipe = urb->UrbIsochronousTransfer.PipeHandle;
		break;
	default:
		return FALSE;
	}

	if (PIPE2ADDR(hPipe) == epaddr)
		return TRUE;
	return FALSE;
}

NTSTATUS
submit_urbr(pvpdo_dev_t vpdo, struct urb_req *urbr)
{
	KIRQL	oldirql;
	KIRQL	oldirql_cancel;
	PIRP	read_irp;
	NTSTATUS	status = STATUS_PENDING;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (vpdo->urbr_sent_partial || vpdo->pending_read_irp == NULL) {
		if (urbr->irp != NULL) {
			IoAcquireCancelSpinLock(&oldirql_cancel);
			IoSetCancelRoutine(urbr->irp, cancel_urbr);
			IoReleaseCancelSpinLock(oldirql_cancel);
			IoMarkIrpPending(urbr->irp);
		}
		InsertTailList(&vpdo->head_urbr_pending, &urbr->list_state);
		InsertTailList(&vpdo->head_urbr, &urbr->list_all);
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		DBGI(DBG_URB, "submit_urbr: urb pending\n");
		return STATUS_PENDING;
	}

	BOOLEAN valid_irp;
	IoAcquireCancelSpinLock(&oldirql_cancel);
	valid_irp = IoSetCancelRoutine(vpdo->pending_read_irp, NULL) != NULL;
	IoReleaseCancelSpinLock(oldirql_cancel);
	if (!valid_irp) {
		DBGI(DBG_URB, "submit_urbr: read irp was cancelled");
		status = STATUS_INVALID_PARAMETER;
		vpdo->pending_read_irp = NULL;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
		return status;
	}

	read_irp = vpdo->pending_read_irp;
	vpdo->urbr_sent_partial = urbr;

	urbr->seq_num = ++(vpdo->seq_num);

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	status = store_urbr(read_irp, urbr);

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (status == STATUS_SUCCESS) {
		if (urbr->irp != NULL) {
			IoAcquireCancelSpinLock(&oldirql_cancel);
			IoSetCancelRoutine(urbr->irp, cancel_urbr);
			IoReleaseCancelSpinLock(oldirql_cancel);
			IoMarkIrpPending(urbr->irp);
		}
		if (vpdo->len_sent_partial == 0) {
			vpdo->urbr_sent_partial = NULL;
			InsertTailList(&vpdo->head_urbr_sent, &urbr->list_state);
		}

		InsertTailList(&vpdo->head_urbr, &urbr->list_all);

		vpdo->pending_read_irp = NULL;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		read_irp->IoStatus.Status = STATUS_SUCCESS;
		IoCompleteRequest(read_irp, IO_NO_INCREMENT);
		status = STATUS_PENDING;
	}
	else {
		vpdo->urbr_sent_partial = NULL;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		status = STATUS_INVALID_PARAMETER;
	}
	DBGI(DBG_URB, "submit_urbr: urb requested: status:%s\n", dbg_ntstatus(status));
	return status;
}
