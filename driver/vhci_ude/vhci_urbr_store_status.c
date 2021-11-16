#include "vhci_urbr_store_status.h"
#include "vhci_driver.h"
#include "vhci_urbr_store_status.tmh"

#include <usbip_proto.h>
#include "vhci_urbr.h"
#include "vhci_proto.h"

NTSTATUS
store_urbr_get_status(WDFREQUEST req_read, purb_req_t urbr)
{
	struct _URB_CONTROL_GET_STATUS_REQUEST	*urb_status = &urbr->u.urb.urb->UrbControlGetStatusRequest;
	USHORT		urbfunc;
	char		recip;

	struct usbip_header *hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = get_submit_setup(hdr);

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, true, NULL,
		USBD_SHORT_TRANSFER_OK, urb_status->TransferBufferLength);

	urbfunc = urb_status->Hdr.Function;
	TraceInfo(TRACE_READ, "urbr: %!URBR!", urbr);

	switch (urbfunc) {
	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
		recip = BMREQUEST_TO_DEVICE;
		break;
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
		recip = BMREQUEST_TO_INTERFACE;
		break;
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
		recip = BMREQUEST_TO_ENDPOINT;
		break;
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
		recip = BMREQUEST_TO_OTHER;
		break;
	default:
		TraceWarning(TRACE_READ, "unhandled %!urb_function!: len: %d", urbfunc, urb_status->Hdr.Length);
		return STATUS_INVALID_PARAMETER;
	}

	build_setup_packet(setup, BMREQUEST_DEVICE_TO_HOST, BMREQUEST_STANDARD, recip, USB_REQUEST_GET_STATUS);
	setup->wLength = (USHORT)urb_status->TransferBufferLength;
	setup->wIndex.W = urb_status->Index;
	setup->wValue.W = 0;

	WdfRequestSetInformation(req_read, sizeof(*hdr));
	return STATUS_SUCCESS;
}
