#include "vhci_urbr_fetch_status.h"
#include "vhci_driver.h"

#include <usbip_proto.h>
#include "vhci_urbr.h"
#include "vhci_urbr_fetch.h"

NTSTATUS
fetch_urbr_status(PURB urb, struct usbip_header *hdr)
{
	struct _URB_CONTROL_GET_STATUS_REQUEST	*urb_status = &urb->UrbControlGetStatusRequest;
	NTSTATUS	status;

	status = copy_to_transfer_buffer(urb_status->TransferBuffer, urb_status->TransferBufferMDL,
		urb_status->TransferBufferLength, hdr + 1, hdr->u.ret_submit.actual_length);
	if (status == STATUS_SUCCESS)
		urb_status->TransferBufferLength = hdr->u.ret_submit.actual_length;
	return status;
}
