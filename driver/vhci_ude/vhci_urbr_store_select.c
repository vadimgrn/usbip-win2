#include "vhci_urbr_store_select.h"
#include "vhci_driver.h"

#include <usbip_proto.h>

#include "vhci_urbr.h"
#include "vhci_proto.h"

NTSTATUS
store_urbr_select_config(WDFREQUEST req_read, purb_req_t urbr)
{
	struct usbip_header *hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = get_submit_setup(hdr);

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, false, 0, 0, 0);

	build_setup_packet(setup, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_STANDARD, BMREQUEST_TO_DEVICE, USB_REQUEST_SET_CONFIGURATION);
	setup->wLength = 0;
	setup->wValue.W = urbr->u.conf_value;
	setup->wIndex.W = 0;

	WdfRequestSetInformation(req_read, sizeof(*hdr));
	return STATUS_SUCCESS;
}

NTSTATUS
store_urbr_select_interface(WDFREQUEST req_read, purb_req_t urbr)
{
	struct usbip_header *hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = get_submit_setup(hdr);

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, 0, 0, 0, 0);

	build_setup_packet(setup, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_STANDARD, BMREQUEST_TO_INTERFACE, USB_REQUEST_SET_INTERFACE);
	setup->wLength = 0;
	setup->wValue.W = urbr->u.intf.alt_setting;
	setup->wIndex.W = urbr->u.intf.intf_num;

	WdfRequestSetInformation(req_read, sizeof(*hdr));
	return  STATUS_SUCCESS;
}
