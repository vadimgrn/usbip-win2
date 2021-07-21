#include "vhci_urbr_store_select.h"
#include "vhci_driver.h"

#include <usbip_proto.h>

#include "vhci_urbr.h"
#include "vhci_proto.h"

NTSTATUS
store_urbr_select_config(WDFREQUEST req_read, purb_req_t urbr)
{
	struct usbip_header	*hdr;
	usb_cspkt_t	*csp;

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, false, 0, 0, 0);
	build_setup_packet(csp, 0, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_SET_CONFIGURATION);
	csp->wLength = 0;
	csp->wValue.W = urbr->u.conf_value;
	csp->wIndex.W = 0;

	WdfRequestSetInformation(req_read, sizeof(struct usbip_header));
	return STATUS_SUCCESS;
}

NTSTATUS
store_urbr_select_interface(WDFREQUEST req_read, purb_req_t urbr)
{
	struct usbip_header *hdr;
	usb_cspkt_t *csp;

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, 0, 0, 0, 0);
	build_setup_packet(csp, 0, BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_SET_INTERFACE);
	csp->wLength = 0;
	csp->wValue.W = urbr->u.intf.alt_setting;
	csp->wIndex.W = urbr->u.intf.intf_num;

	WdfRequestSetInformation(req_read, sizeof(struct usbip_header));
	return  STATUS_SUCCESS;
}
