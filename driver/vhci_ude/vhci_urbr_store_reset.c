#include "vhci_urbr_store_reset.h"
#include "vhci_driver.h"

#include <usbip_proto.h>
#include "vhci_urbr.h"
#include "vhci_proto.h"

NTSTATUS
store_urbr_reset_pipe(WDFREQUEST req_read, purb_req_t urbr)
{
	struct usbip_header *hdr;
	usb_cspkt_t *csp;

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	csp = (usb_cspkt_t *)hdr->u.cmd_submit.setup;

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, 0, 0, 0, 0);
	build_setup_packet(csp, 0, BMREQUEST_STANDARD, BMREQUEST_TO_ENDPOINT, USB_REQUEST_CLEAR_FEATURE);
	csp->wIndex.LowByte = urbr->ep->addr; // Specify enpoint address and direction
	csp->wIndex.HiByte = 0;
	csp->wValue.W = 0; // clear ENDPOINT_HALT
	csp->wLength = 0;

	WdfRequestSetInformation(req_read, sizeof(struct usbip_header));

	return STATUS_SUCCESS;
}
