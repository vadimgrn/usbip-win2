#include "vhci_urbr_store_dscr.h"
#include "vhci_driver.h"

#include "usbip_proto.h"
#include "vhci_urbr.h"
#include "vhci_proto.h"

NTSTATUS
store_urbr_dscr_dev(WDFREQUEST req_read, purb_req_t urbr)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST	*urb_dscr = &urbr->u.urb.urb->UrbControlDescriptorRequest;
	struct usbip_header	*hdr;
	usb_cspkt_t	*csp;

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, true, NULL,
		USBD_SHORT_TRANSFER_OK, urb_dscr->TransferBufferLength);
	
	build_setup_packet(csp, USBIP_DIR_IN, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_GET_DESCRIPTOR);

	csp->wLength = (unsigned short)urb_dscr->TransferBufferLength;
	csp->wValue.HiByte = urb_dscr->DescriptorType;
	csp->wValue.LowByte = urb_dscr->Index;

	switch (urb_dscr->DescriptorType) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		csp->wIndex.W = 0;
		break;
	case USB_INTERFACE_DESCRIPTOR_TYPE:
		csp->wIndex.W = urb_dscr->Index;
		break;
	case USB_STRING_DESCRIPTOR_TYPE:
		csp->wIndex.W = urb_dscr->LanguageId;
		break;
	default:
		return STATUS_INVALID_PARAMETER;
	}

	WdfRequestSetInformation(req_read, sizeof(struct usbip_header));
	return STATUS_SUCCESS;
}

NTSTATUS
store_urbr_dscr_intf(WDFREQUEST req_read, purb_req_t urbr)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST	*urb_dscr = &urbr->u.urb.urb->UrbControlDescriptorRequest;
	struct usbip_header	*hdr;
	usb_cspkt_t	*csp;

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, USBIP_DIR_IN, NULL,
		USBD_SHORT_TRANSFER_OK, urb_dscr->TransferBufferLength);
	build_setup_packet(csp, USBIP_DIR_IN, BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_GET_DESCRIPTOR);

	csp->wLength = (unsigned short)urb_dscr->TransferBufferLength;
	csp->wValue.HiByte = urb_dscr->DescriptorType;
	csp->wValue.LowByte = urb_dscr->Index;
	csp->wIndex.W = urb_dscr->LanguageId;

	WdfRequestSetInformation(req_read, sizeof(struct usbip_header));
	return STATUS_SUCCESS;
}
