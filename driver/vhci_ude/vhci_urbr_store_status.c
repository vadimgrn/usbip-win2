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
	struct usbip_header	*hdr;
	USHORT		urbfunc;
	char		recip;
	usb_cspkt_t	*csp;

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, true, NULL,
		USBD_SHORT_TRANSFER_OK, urb_status->TransferBufferLength);

	urbfunc = urb_status->Hdr.Function;
	TRD(READ, "urbr: %!URBR!", urbr);

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
		TRW(READ, "unhandled urb function: %!URBFUNC!: len: %d", urbfunc, urb_status->Hdr.Length);
		return STATUS_INVALID_PARAMETER;
	}

	build_setup_packet(csp, USBIP_DIR_IN, BMREQUEST_STANDARD, recip, USB_REQUEST_GET_STATUS);

	csp->wLength = (unsigned short)urb_status->TransferBufferLength;
	csp->wIndex.W = urb_status->Index;
	csp->wValue.W = 0;

	WdfRequestSetInformation(req_read, sizeof(struct usbip_header));
	return STATUS_SUCCESS;
}
