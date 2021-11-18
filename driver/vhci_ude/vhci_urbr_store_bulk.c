#include "vhci_urbr_store_bulk.h"
#include "vhci_driver.h"
#include "vhci_urbr_store_bulk.tmh"

#include <usbip_proto.h>
#include "vhci_urbr.h"
#include "vhci_proto.h"
#include "usbd_helper.h"

NTSTATUS
store_urbr_bulk_partial(WDFREQUEST req_read, purb_req_t urbr)
{
	struct _URB_BULK_OR_INTERRUPT_TRANSFER	*urb_bi = &urbr->u.urb.urb->UrbBulkOrInterruptTransfer;
	PVOID	dst, src;
	NTSTATUS	status;

	status = WdfRequestRetrieveOutputBuffer(req_read, urb_bi->TransferBufferLength, &dst, NULL);
	if (NT_ERROR(status))
		return STATUS_BUFFER_TOO_SMALL;

	src = get_buf(urb_bi->TransferBuffer, urb_bi->TransferBufferMDL);
	if (src == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;
	RtlCopyMemory(dst, src, urb_bi->TransferBufferLength);
	WdfRequestSetInformation(req_read, urb_bi->TransferBufferLength);
	urbr->ep->vusb->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

NTSTATUS
store_urbr_bulk(WDFREQUEST req_read, purb_req_t urbr)
{
	struct usbip_header *hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	int type = urbr->ep->type;
	if (type != USB_ENDPOINT_TYPE_BULK && type != USB_ENDPOINT_TYPE_INTERRUPT) {
		TraceError(TRACE_READ, "Error, not a bulk or a interrupt pipe");
		return STATUS_INVALID_PARAMETER;
	}

	struct _URB_BULK_OR_INTERRUPT_TRANSFER *urb_bi = &urbr->u.urb.urb->UrbBulkOrInterruptTransfer;

	/* Sometimes, direction in TransferFlags of _URB_BULK_OR_INTERRUPT_TRANSFER is not consistent with PipeHandle.
	 * Use a direction flag in pipe handle.
	 */
	bool dir_in = IsTransferDirectionIn(urb_bi->TransferFlags);

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, dir_in, urbr->ep,
		urb_bi->TransferFlags, urb_bi->TransferBufferLength);
	
	RtlZeroMemory(hdr->u.cmd_submit.setup, sizeof(hdr->u.cmd_submit.setup));

	WdfRequestSetInformation(req_read, sizeof(struct usbip_header));

	if (!dir_in) {
		if (get_read_payload_length(req_read) >= urb_bi->TransferBufferLength) {
			PVOID	buf = get_buf(urb_bi->TransferBuffer, urb_bi->TransferBufferMDL);
			if (buf == NULL)
				return STATUS_INSUFFICIENT_RESOURCES;
			RtlCopyMemory(hdr + 1, buf, urb_bi->TransferBufferLength);
		}
		else {
			urbr->ep->vusb->len_sent_partial = sizeof(struct usbip_header);
		}
	}
	return STATUS_SUCCESS;
}
