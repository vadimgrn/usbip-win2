#include "vhci_urbr.h"
#include "vhci_driver.h"
#include "vhci_urbr.tmh"

#include "usbip_proto.h"
#include "vhci_urbr_store.h"

PVOID
get_buf(PVOID buf, PMDL bufMDL)
{
	if (buf == NULL) {
		if (bufMDL != NULL)
			buf = MmGetSystemAddressForMdlSafe(bufMDL, LowPagePriority);
		if (buf == NULL) {
			TraceError(READ, "No transfer buffer\n");
		}
	}
	return buf;
}

struct usbip_header *
get_hdr_from_req_read(WDFREQUEST req_read)
{
	struct usbip_header *hdr;
	NTSTATUS	status;

	status = WdfRequestRetrieveOutputBuffer(req_read, sizeof(struct usbip_header), &hdr, NULL);
	if (NT_ERROR(status)) {
		return NULL;
	}
	return hdr;
}

PVOID
get_data_from_req_read(WDFREQUEST req_read, ULONG length)
{
	PVOID	data;
	NTSTATUS	status;

	status = WdfRequestRetrieveOutputBuffer(req_read, length, &data, NULL);
	if (NT_ERROR(status)) {
		return NULL;
	}
	return data;
}

ULONG
get_read_payload_length(WDFREQUEST req_read)
{
	WDF_REQUEST_PARAMETERS	params;

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(req_read, &params);

	return (ULONG)(params.Parameters.Read.Length - sizeof(struct usbip_header));
}

void build_setup_packet(USB_DEFAULT_PIPE_SETUP_PACKET *setup, UCHAR dir, UCHAR type, UCHAR recip, UCHAR request)
{
	setup->bmRequestType.B = 0; // clear reserved bits as well

	setup->bmRequestType.Dir = dir;
	setup->bmRequestType.Type = type;
	setup->bmRequestType.Recipient = recip;

	setup->bRequest = request;
}

purb_req_t
find_sent_urbr(pctx_vusb_t vusb, struct usbip_header *hdr)
{
	PLIST_ENTRY	le;

	WdfSpinLockAcquire(vusb->spin_lock);
	for (le = vusb->head_urbr_sent.Flink; le != &vusb->head_urbr_sent; le = le->Flink) {
		purb_req_t	urbr;
		urbr = CONTAINING_RECORD(le, urb_req_t, list_state);
		if (urbr->seq_num == hdr->base.seqnum) {
			RemoveEntryListInit(&urbr->list_all);
			RemoveEntryListInit(&urbr->list_state);
			WdfSpinLockRelease(vusb->spin_lock);
			return urbr;
		}
	}
	WdfSpinLockRelease(vusb->spin_lock);

	return NULL;
}

static PURB
get_urb_from_req(WDFREQUEST req)
{
	WDF_REQUEST_PARAMETERS	params;

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(req, &params);
	if (params.Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_SUBMIT_URB)
		return (PURB)params.Parameters.Others.Arg1;
	return NULL;
}

static purb_req_t
create_urbr(pctx_ep_t ep, urbr_type_t type, WDFREQUEST req)
{
	WDFMEMORY	hmem;
	purb_req_t	urbr;
	NTSTATUS	status;

	if (ep == NULL || ep->vusb == NULL) {
		TraceError(URBR, "failed to allocate memory for urbr:%p", ep);
		return NULL;
	}
	status = WdfMemoryCreateFromLookaside(ep->vusb->lookaside_urbr, &hmem);
	if (NT_ERROR(status)) {
		TraceError(URBR, "failed to allocate memory for urbr: %!STATUS!", status);
		return NULL;
	}

	urbr = TO_URBR(hmem);
	RtlZeroMemory(urbr, sizeof(urb_req_t));
	urbr->type = type;
	urbr->hmem = hmem;
	urbr->ep = ep;
	urbr->req = req;
	if (type == URBR_TYPE_URB) {
		urbr->u.urb.urb = get_urb_from_req(req);
		urbr->u.urb.cancelable = FALSE;
		WdfRequestSetInformation(req, (ULONG_PTR)urbr);
	}

	InitializeListHead(&urbr->list_all);
	InitializeListHead(&urbr->list_state);

	return urbr;
}

static void
free_urbr(purb_req_t urbr)
{
	ASSERT(IsListEmpty(&urbr->list_all));
	ASSERT(IsListEmpty(&urbr->list_state));
	WdfObjectDelete(urbr->hmem);
}

static void
submit_urbr_unlink(pctx_ep_t ep, unsigned long seq_num_unlink)
{
	purb_req_t	urbr_unlink;

	urbr_unlink = create_urbr(ep, URBR_TYPE_UNLINK, NULL);
	if (urbr_unlink != NULL) {
		NTSTATUS	status;

		urbr_unlink->u.seq_num_unlink = seq_num_unlink;
		status = submit_urbr(urbr_unlink);
		if (NT_ERROR(status)) {
			TraceInfo(URBR, "failed to submit unlink urb: %!URBR!", urbr_unlink);
			free_urbr(urbr_unlink);
		}
	}
}

static VOID
urbr_cancelled(_In_ WDFREQUEST req)
{
	purb_req_t	urbr = (purb_req_t)WdfRequestGetInformation(req);
	pctx_vusb_t	vusb = urbr->ep->vusb;

	WdfSpinLockAcquire(vusb->spin_lock);
	RemoveEntryListInit(&urbr->list_state);
	RemoveEntryListInit(&urbr->list_all);
	if (vusb->urbr_sent_partial == urbr) {
		vusb->urbr_sent_partial = NULL;
		vusb->len_sent_partial = 0;
	}
	WdfSpinLockRelease(vusb->spin_lock);

	if (urbr != NULL && urbr->seq_num != 0) {
		submit_urbr_unlink(urbr->ep, urbr->seq_num);
		TraceInfo(URBR, "cancelled urbr destroyed: %!URBR!", urbr);
		complete_urbr(urbr, STATUS_CANCELLED);
	}
	else {
		UdecxUrbCompleteWithNtStatus(req, STATUS_CANCELLED);
	}
}

static BOOLEAN
mark_cancelable_urbr(purb_req_t urbr)
{
	NTSTATUS	status;

	if (urbr->type != URBR_TYPE_URB)
		return TRUE;

	ASSERT(!urbr->u.urb.cancelable);

	status = WdfRequestMarkCancelableEx(urbr->req, urbr_cancelled);
	if (NT_ERROR(status)) {
		TraceInfo(URBR, "Already cancelled request?: %!URBR!, %!STATUS!", urbr, status);
		return FALSE;
	}
	urbr->u.urb.cancelable = TRUE;
	return TRUE;
}

NTSTATUS
submit_urbr(purb_req_t urbr)
{
	pctx_vusb_t	vusb = urbr->ep->vusb;
	WDFREQUEST	req_read;
	NTSTATUS	status = STATUS_PENDING;

	WdfSpinLockAcquire(vusb->spin_lock);

	if (vusb->invalid) {
		WdfSpinLockRelease(vusb->spin_lock);
		TraceInfo(URBR, "failed to submit urbr: invalidated vusb");
		return STATUS_DEVICE_NOT_CONNECTED;
	}

	if (vusb->urbr_sent_partial || vusb->pending_req_read == NULL) {
		if (!mark_cancelable_urbr(urbr)) {
			WdfSpinLockRelease(vusb->spin_lock);
			return STATUS_CANCELLED;
		}
		InsertTailList(&vusb->head_urbr_pending, &urbr->list_state);
		InsertTailList(&vusb->head_urbr, &urbr->list_all);
		WdfSpinLockRelease(vusb->spin_lock);

		TraceInfo(URBR, "urb pending: %!URBR!", urbr);
		return STATUS_PENDING;
	}

	req_read = vusb->pending_req_read;
	vusb->urbr_sent_partial = urbr;

	urbr->seq_num = ++(vusb->seq_num);

	WdfSpinLockRelease(vusb->spin_lock);

	status = store_urbr(req_read, urbr);

	WdfSpinLockAcquire(vusb->spin_lock);

	if (status == STATUS_SUCCESS) {
		if (!mark_cancelable_urbr(urbr)) {
			WdfSpinLockRelease(vusb->spin_lock);
			return STATUS_CANCELLED;
		}
		if (vusb->len_sent_partial == 0) {
			vusb->urbr_sent_partial = NULL;
			InsertTailList(&vusb->head_urbr_sent, &urbr->list_state);
		}

		InsertTailList(&vusb->head_urbr, &urbr->list_all);

		vusb->pending_req_read = NULL;
		WdfSpinLockRelease(vusb->spin_lock);

		WdfRequestUnmarkCancelable(req_read);
		WdfRequestComplete(req_read, STATUS_SUCCESS);
		status = STATUS_PENDING;
	}
	else {
		vusb->urbr_sent_partial = NULL;
		WdfSpinLockRelease(vusb->spin_lock);

		if (status == STATUS_FLT_IO_COMPLETE)
			status = STATUS_SUCCESS;
		else
			status = STATUS_INVALID_PARAMETER;
	}

	TraceInfo(URBR, "urb requested: %!URBR!: %!STATUS!", urbr, status);
	return status;
}

static NTSTATUS
submit_urbr_free(purb_req_t urbr)
{
	NTSTATUS	status;

	status = submit_urbr(urbr);
	if (NT_ERROR(status))
		free_urbr(urbr);
	return status;
}

NTSTATUS
submit_req_urb(pctx_ep_t ep, WDFREQUEST req)
{
	purb_req_t	urbr;

	urbr = create_urbr(ep, URBR_TYPE_URB, req);
	if (urbr == NULL)
		return STATUS_UNSUCCESSFUL;
	return submit_urbr_free(urbr);
}

NTSTATUS
submit_req_select(pctx_ep_t ep, WDFREQUEST req, BOOLEAN is_select_conf, UCHAR conf_value, UCHAR intf_num, UCHAR alt_setting)
{
	purb_req_t	urbr;

	urbr = create_urbr(ep, is_select_conf ? URBR_TYPE_SELECT_CONF: URBR_TYPE_SELECT_INTF, req);
	if (urbr == NULL)
		return STATUS_UNSUCCESSFUL;

	if (is_select_conf) {
		urbr->u.conf_value = conf_value;
	}
	else {
		urbr->u.intf.intf_num = intf_num;
		urbr->u.intf.alt_setting = alt_setting;
	}
	return submit_urbr_free(urbr);
}

NTSTATUS
submit_req_reset_pipe(pctx_ep_t ep, WDFREQUEST req)
{
	purb_req_t	urbr;

	urbr = create_urbr(ep, URBR_TYPE_RESET_PIPE, req);
	if (urbr == NULL)
		return STATUS_UNSUCCESSFUL;

	return submit_urbr_free(urbr);
}

BOOLEAN
unmark_cancelable_urbr(purb_req_t urbr)
{
	WDFREQUEST	req;
	NTSTATUS	status;

	req = urbr->req;
	if (req == NULL)
		return TRUE;
	if (urbr->type != URBR_TYPE_URB || !urbr->u.urb.cancelable)
		return TRUE;
	status = WdfRequestUnmarkCancelable(req);
	urbr->u.urb.cancelable = FALSE;
	if (status == STATUS_CANCELLED)
		return FALSE;
	return TRUE;
}

void
complete_urbr(purb_req_t urbr, NTSTATUS status)
{
	WDFREQUEST	req;

	req = urbr->req;
	if (req != NULL) {
		if (urbr->type != URBR_TYPE_URB)
			WdfRequestComplete(req, status);
		else {
			if (status == STATUS_SUCCESS)
				UdecxUrbComplete(req, urbr->u.urb.urb->UrbHeader.Status);
			else {
				UdecxUrbCompleteWithNtStatus(req, status);
			}
		}
	}
	free_urbr(urbr);
}
