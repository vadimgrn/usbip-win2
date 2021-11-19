#include "vhci_urbr_store_control.h"
#include "vhci_driver.h"
#include "vhci_urbr_store_control.tmh"

#include <usbip_proto.h>
#include "vhci_urbr.h"
#include "vhci_urbr_fetch.h"
#include "vhci_proto.h"

#include "strutil.h"
#include "usbd_helper.h"

#include <ntstrsafe.h>

NTSTATUS
store_urbr_control_transfer_partial(WDFREQUEST req_read, purb_req_t urbr)
{
	struct _URB_CONTROL_TRANSFER	*urb_ctltrans = &urbr->u.urb.urb->UrbControlTransfer;
	PVOID	dst;
	char	*buf;

	dst = get_data_from_req_read(req_read, urb_ctltrans->TransferBufferLength);
	if (dst == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	/*
	 * reading from TransferBuffer or TransferBufferMDL,
	 * whichever of them is not null
	 */
	buf = get_buf(urb_ctltrans->TransferBuffer, urb_ctltrans->TransferBufferMDL);
	if (buf == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;
	RtlCopyMemory(dst, buf, urb_ctltrans->TransferBufferLength);
	WdfRequestSetInformation(req_read, urb_ctltrans->TransferBufferLength);
	urbr->ep->vusb->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

NTSTATUS
store_urbr_control_transfer_ex_partial(WDFREQUEST req_read, purb_req_t urbr)
{
	struct _URB_CONTROL_TRANSFER_EX	*urb_ctltrans_ex = &urbr->u.urb.urb->UrbControlTransferEx;
	PVOID	dst;
	char	*buf;

	dst = get_data_from_req_read(req_read, urb_ctltrans_ex->TransferBufferLength);
	if (dst == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	/*
	 * reading from TransferBuffer or TransferBufferMDL,
	 * whichever of them is not null
	 */
	buf = get_buf(urb_ctltrans_ex->TransferBuffer, urb_ctltrans_ex->TransferBufferMDL);
	if (buf == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;
	RtlCopyMemory(dst, buf, urb_ctltrans_ex->TransferBufferLength);
	WdfRequestSetInformation(req_read, urb_ctltrans_ex->TransferBufferLength);
	urbr->ep->vusb->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

NTSTATUS
store_urbr_control_transfer(WDFREQUEST req_read, purb_req_t urbr)
{
	struct _URB_CONTROL_TRANSFER	*urb_ctltrans = &urbr->u.urb.urb->UrbControlTransfer;
	struct usbip_header	*hdr;
	bool dir_in = IsTransferDirectionIn(urb_ctltrans->TransferFlags);
	ULONG	nread = 0;
	NTSTATUS	status = STATUS_SUCCESS;

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, dir_in, urbr->ep,
		urb_ctltrans->TransferFlags | USBD_SHORT_TRANSFER_OK, urb_ctltrans->TransferBufferLength);
	
	RtlCopyMemory(hdr->u.cmd_submit.setup, urb_ctltrans->SetupPacket, sizeof(urb_ctltrans->SetupPacket));
	static_assert(sizeof(hdr->u.cmd_submit.setup) == sizeof(urb_ctltrans->SetupPacket), "assert");

	nread = sizeof(struct usbip_header);
	if (!dir_in && urb_ctltrans->TransferBufferLength > 0) {
		if (get_read_payload_length(req_read) >= urb_ctltrans->TransferBufferLength) {
			PVOID	buf = get_buf(urb_ctltrans->TransferBuffer, urb_ctltrans->TransferBufferMDL);
			if (buf == NULL) {
				status = STATUS_INSUFFICIENT_RESOURCES;
				goto out;
			}
			nread += urb_ctltrans->TransferBufferLength;
			RtlCopyMemory(hdr + 1, buf, urb_ctltrans->TransferBufferLength);
		}
		else {
			urbr->ep->vusb->len_sent_partial = sizeof(struct usbip_header);
		}
	}
out:
	WdfRequestSetInformation(req_read, nread);
	return status;
}

static bool is_serial_setup_pkt(UCHAR iSerial, const UCHAR *setup)
{
	const USB_DEFAULT_PIPE_SETUP_PACKET *p = (const USB_DEFAULT_PIPE_SETUP_PACKET*)setup;

	return  p->bmRequestType.Dir == BMREQUEST_DEVICE_TO_HOST && 
		p->bmRequestType.Type == BMREQUEST_STANDARD && 
		p->bmRequestType.Recipient == BMREQUEST_TO_DEVICE && 
		p->bRequest == USB_REQUEST_GET_DESCRIPTOR &&
		p->wValue.W == USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(USB_STRING_DESCRIPTOR_TYPE, iSerial);
}

static NTSTATUS fetch_done_urbr_control_transfer_ex(ctx_vusb_t *vusb, struct _URB_CONTROL_TRANSFER_EX *urb_ctltrans_ex)
{
	size_t str_sz = 0;
	NTSTATUS status = RtlStringCbLengthW(vusb->wserial, MAXIMUM_USB_STRING_LENGTH, &str_sz);
	if (status != STATUS_SUCCESS) {
		TraceError(TRACE_READ, "Can't get length of wserial '%!WSTR!': %!STATUS!", vusb->wserial, status);
		return status;
	}

	static_assert(MAXIMUM_USB_STRING_LENGTH <= MAXUCHAR, "assert");

	USB_STRING_DESCRIPTOR *sd = NULL;
	ULONG sd_sz = sizeof(*sd) - sizeof(sd->bString) + (ULONG)str_sz;

	if (sd_sz > MAXIMUM_USB_STRING_LENGTH) { // bLength can't hold this value
		TraceError(TRACE_READ, "String descriptor size %lu exceeds limit %d for wserial '%!WSTR!'", 
			sd_sz, MAXIMUM_USB_STRING_LENGTH, vusb->wserial);

		return STATUS_INVALID_PARAMETER;
	}

	UCHAR buf[MAXIMUM_USB_STRING_LENGTH];
	sd = (USB_STRING_DESCRIPTOR*)buf;

	sd->bLength = (UCHAR)sd_sz;
	sd->bDescriptorType = USB_STRING_DESCRIPTOR_TYPE;
	RtlCopyMemory(sd->bString, vusb->wserial, str_sz);

	status = copy_to_transfer_buffer(urb_ctltrans_ex->TransferBuffer, urb_ctltrans_ex->TransferBufferMDL,
		urb_ctltrans_ex->TransferBufferLength, sd, sd_sz);

	if (status == STATUS_SUCCESS) {
		urb_ctltrans_ex->TransferBufferLength = sd_sz;
		status = STATUS_FLT_IO_COMPLETE; // lets urbr be completed without fetching
	}

	return status;
}

NTSTATUS
store_urbr_control_transfer_ex(WDFREQUEST req_read, purb_req_t urbr)
{
	pctx_vusb_t	vusb = urbr->ep->vusb;
	struct _URB_CONTROL_TRANSFER_EX	*urb_ctltrans_ex = &urbr->u.urb.urb->UrbControlTransferEx;
	struct usbip_header	*hdr;
	bool dir_in = IsTransferDirectionIn(urb_ctltrans_ex->TransferFlags);
	ULONG	nread = 0;
	NTSTATUS	status = STATUS_SUCCESS;

	/*
	 * overwrite USB serial if applicable
	 * UDE vhub seems to request a serial string via URB_FUNCTION_CONTROL_TRANSFER_EX.
	 */
	if (vusb->iSerial > 0 && vusb->wserial && is_serial_setup_pkt(vusb->iSerial, urb_ctltrans_ex->SetupPacket)) {
		TraceInfo(TRACE_READ, "overwrite serial string: %S", vusb->wserial);
		return fetch_done_urbr_control_transfer_ex(vusb, urb_ctltrans_ex);
	}

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, dir_in, urbr->ep,
		urb_ctltrans_ex->TransferFlags, urb_ctltrans_ex->TransferBufferLength);

	RtlCopyMemory(hdr->u.cmd_submit.setup, urb_ctltrans_ex->SetupPacket, sizeof(urb_ctltrans_ex->SetupPacket));
	static_assert(sizeof(hdr->u.cmd_submit.setup) == sizeof(urb_ctltrans_ex->SetupPacket), "assert");

	nread = sizeof(struct usbip_header);
	if (!dir_in && urb_ctltrans_ex->TransferBufferLength > 0) {
		if (get_read_payload_length(req_read) >= urb_ctltrans_ex->TransferBufferLength) {
			PVOID	buf = get_buf(urb_ctltrans_ex->TransferBuffer, urb_ctltrans_ex->TransferBufferMDL);
			if (buf == NULL) {
				status = STATUS_INSUFFICIENT_RESOURCES;
				goto out;
			}
			nread += urb_ctltrans_ex->TransferBufferLength;
			RtlCopyMemory(hdr + 1, buf, urb_ctltrans_ex->TransferBufferLength);
		}
		else {
			urbr->ep->vusb->len_sent_partial = sizeof(struct usbip_header);
		}
	}
out:
	WdfRequestSetInformation(req_read, nread);
	return status;
}
