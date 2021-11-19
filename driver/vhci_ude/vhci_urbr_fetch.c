#include "vhci_urbr_fetch.h"
#include "vhci_driver.h"
#include "vhci_urbr_fetch.tmh"

#include "usbip_proto.h"
#include "vhci_urbr.h"
#include "usbd_helper.h"

#include "vhci_urbr_fetch_status.h"
#include "vhci_urbr_fetch_dscr.h"
#include "vhci_urbr_fetch_control.h"
#include "vhci_urbr_fetch_vendor.h"
#include "vhci_urbr_fetch_bulk.h"
#include "vhci_urbr_fetch_iso.h"

NTSTATUS copy_to_transfer_buffer(void *buf_dst, MDL *bufMDL, ULONG dst_len, const void *src, ULONG src_len)
{
	if (dst_len < src_len) {
		TraceError(TRACE_WRITE, "Buffer too small: dest %lu, src %lu", dst_len, src_len);
		return STATUS_BUFFER_TOO_SMALL;
	}
	
	void *buf = get_buf(buf_dst, bufMDL);
	if (buf) {
		RtlCopyMemory(buf, src, src_len);
		return STATUS_SUCCESS;
	}

	return STATUS_INVALID_PARAMETER;
}

static NTSTATUS
fetch_urbr_urb(PURB urb, struct usbip_header *hdr)
{
	NTSTATUS	status;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
		status = fetch_urbr_status(urb, hdr);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
		status = fetch_urbr_dscr(urb, hdr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER:
		status = fetch_urbr_control_transfer(urb, hdr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		status = fetch_urbr_control_transfer_ex(urb, hdr);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
	case URB_FUNCTION_VENDOR_OTHER:
		status = fetch_urbr_vendor_or_class(urb, hdr);
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = fetch_urbr_bulk_or_interrupt(urb, hdr);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = fetch_urbr_iso(urb, hdr);
		break;
#if 0
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
		status = STATUS_SUCCESS;
		break;
#endif
	default:
		TraceWarning(TRACE_WRITE, "not supported %!urb_function!", urb->UrbHeader.Function);
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	if (status == STATUS_SUCCESS)
		urb->UrbHeader.Status = to_windows_status(hdr->u.ret_submit.status);

	return status;
}

static VOID
handle_urbr_error(purb_req_t urbr, struct usbip_header *hdr)
{
	PURB	urb = urbr->u.urb.urb;

	urb->UrbHeader.Status = to_windows_status(hdr->u.ret_submit.status);
	if (urb->UrbHeader.Status == USBD_STATUS_STALL_PID) {
		/*
		 * TODO: UDE framework seems to discard URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL.
		 * For a simple vusb, such the problem was observed by an usb packet monitoring tool.
		 * Thus an explicit reset is requested if a STALL occurs.
		 * This workaround resolved some USB disk problems.
		 */
		submit_req_reset_pipe(urbr->ep, NULL);
	}

	char buf[DBG_URBR_BUFSZ];
	TraceWarning(TRACE_WRITE, "%s(%#010lX): %s", dbg_usbd_status(urb->UrbHeader.Status), (ULONG)urb->UrbHeader.Status, dbg_urbr(buf, sizeof(buf), urbr));
}

NTSTATUS
fetch_urbr(purb_req_t urbr, struct usbip_header *hdr)
{
	char buf[DBG_URBR_BUFSZ];
	TraceInfo(TRACE_WRITE, "Enter: %s", dbg_urbr(buf, sizeof(buf), urbr));

	NTSTATUS status = STATUS_SUCCESS;

	if (urbr->type == URBR_TYPE_URB) {
		if (hdr->u.ret_submit.status) {
			handle_urbr_error(urbr, hdr);
		}
		status = fetch_urbr_urb(urbr->u.urb.urb, hdr);
	}

	TraceInfo(TRACE_WRITE, "Leave: %!STATUS!", status);
	return status;
}
