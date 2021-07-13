#include "vhci_urbr_fetch_dscr.h"
#include "vhci_driver.h"

#include <usbip_proto.h>

#include "vhci_urbr.h"
#include "vhci_urbr_fetch.h"

NTSTATUS
fetch_urbr_dscr(PURB urb, struct usbip_header *hdr)
{
	struct _URB_CONTROL_DESCRIPTOR_REQUEST *urb_desc = &urb->UrbControlDescriptorRequest;
	NTSTATUS	status;

	status = copy_to_transfer_buffer(urb_desc->TransferBuffer, urb_desc->TransferBufferMDL,
		urb_desc->TransferBufferLength, hdr + 1, hdr->u.ret_submit.actual_length);
	if (status == STATUS_SUCCESS)
		urb_desc->TransferBufferLength = hdr->u.ret_submit.actual_length;
	return status;
}
