#include "vhci_urbr_store_vendor.h"
#include "vhci_driver.h"
#include "vhci_urbr_store_vendor.tmh"

#include <usbip_proto.h>
#include "vhci_urbr.h"
#include "vhci_proto.h"

NTSTATUS
store_urbr_vendor_class_partial(WDFREQUEST req_read, purb_req_t urbr)
{
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST	*urb_vendor_class = &urbr->u.urb.urb->UrbControlVendorClassRequest;
	PVOID	dst;
	char	*buf;

	dst = get_data_from_req_read(req_read, urb_vendor_class->TransferBufferLength);
	if (dst == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	/*
	 * reading from TransferBuffer or TransferBufferMDL,
	 * whichever of them is not null
	 */
	buf = get_buf(urb_vendor_class->TransferBuffer, urb_vendor_class->TransferBufferMDL);
	if (buf == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	RtlCopyMemory(dst, buf, urb_vendor_class->TransferBufferLength);
	WdfRequestSetInformation(req_read, urb_vendor_class->TransferBufferLength);
	urbr->ep->vusb->len_sent_partial = 0;

	return STATUS_SUCCESS;
}

NTSTATUS
store_urbr_vendor_class(WDFREQUEST req_read, purb_req_t urbr)
{
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST	*urb_vendor_class = &urbr->u.urb.urb->UrbControlVendorClassRequest;
	struct usbip_header	*hdr;
	usb_cspkt_t	*csp;
	char	type, recip;
	int	in = IS_TRANSFER_FLAGS_IN(urb_vendor_class->TransferFlags);

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	switch (urb_vendor_class->Hdr.Function) {
	case URB_FUNCTION_CLASS_DEVICE:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_DEVICE;
		break;
	case URB_FUNCTION_CLASS_INTERFACE:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_INTERFACE;
		break;
	case URB_FUNCTION_CLASS_ENDPOINT:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_ENDPOINT;
		break;
	case URB_FUNCTION_CLASS_OTHER:
		type = BMREQUEST_CLASS;
		recip = BMREQUEST_TO_OTHER;
		break;
	case URB_FUNCTION_VENDOR_DEVICE:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_DEVICE;
		break;
	case URB_FUNCTION_VENDOR_INTERFACE:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_INTERFACE;
		break;
	case URB_FUNCTION_VENDOR_ENDPOINT:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_ENDPOINT;
		break;
	case URB_FUNCTION_VENDOR_OTHER:
		type = BMREQUEST_VENDOR;
		recip = BMREQUEST_TO_OTHER;
		break;
	default:
		return STATUS_INVALID_PARAMETER;
	}

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, in, NULL,
		urb_vendor_class->TransferFlags | USBD_SHORT_TRANSFER_OK, urb_vendor_class->TransferBufferLength);
	build_setup_packet(csp, (unsigned char)in, type, recip, urb_vendor_class->Request);
	//FIXME what is the usage of RequestTypeReservedBits?
	csp->wLength = (unsigned short)urb_vendor_class->TransferBufferLength;
	csp->wValue.W = urb_vendor_class->Value;
	csp->wIndex.W = urb_vendor_class->Index;

	if (!in) {
		if (get_read_payload_length(req_read) >= urb_vendor_class->TransferBufferLength) {
			RtlCopyMemory(hdr + 1, urb_vendor_class->TransferBuffer, urb_vendor_class->TransferBufferLength);
			WdfRequestSetInformation(req_read, sizeof(struct usbip_header) + urb_vendor_class->TransferBufferLength);
		}
		else {
			WdfRequestSetInformation(req_read, sizeof(struct usbip_header));
			urbr->ep->vusb->len_sent_partial = sizeof(struct usbip_header);
		}
	}
	return  STATUS_SUCCESS;
}
