#include "vhci_urbr_store_reset.h"
#include "vhci_driver.h"

#include <usbip_proto.h>
#include "vhci_urbr.h"
#include "vhci_proto.h"

NTSTATUS
store_urbr_reset_pipe(WDFREQUEST req_read, purb_req_t urbr)
{
	struct usbip_header *hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_BUFFER_TOO_SMALL;

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = get_submit_setup(hdr);

	set_cmd_submit_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, false, 0, 0, 0);
	
	build_setup_packet(setup, BMREQUEST_HOST_TO_DEVICE, BMREQUEST_STANDARD, BMREQUEST_TO_ENDPOINT, USB_REQUEST_CLEAR_FEATURE);
	setup->wIndex.LowByte = urbr->ep->addr; // Specify enpoint address and direction
	setup->wIndex.HiByte = 0;
	setup->wValue.W = 0; // clear ENDPOINT_HALT
	setup->wLength = 0;

	WdfRequestSetInformation(req_read, sizeof(*hdr));

	return STATUS_SUCCESS;
}
