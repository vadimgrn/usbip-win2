#include "vhci_urbr_fetch_control.h"
#include "vhci_driver.h"

#include "usbip_proto.h"
#include "vhci_urbr.h"
#include "vhci_urbr_fetch.h"

NTSTATUS
fetch_urbr_control_transfer(PURB urb, struct usbip_header *hdr)
{
	struct _URB_CONTROL_TRANSFER	*urb_ctltrans = &urb->UrbControlTransfer;
	NTSTATUS	status;

	if (!IS_TRANSFER_FLAGS_IN(urb_ctltrans->TransferFlags))
		return STATUS_SUCCESS;
	status = copy_to_transfer_buffer(urb_ctltrans->TransferBuffer, urb_ctltrans->TransferBufferMDL,
		urb_ctltrans->TransferBufferLength, hdr + 1, hdr->u.ret_submit.actual_length);
	if (status == STATUS_SUCCESS)
		urb_ctltrans->TransferBufferLength = hdr->u.ret_submit.actual_length;
	return status;
}

NTSTATUS
fetch_urbr_control_transfer_ex(PURB urb, struct usbip_header *hdr)
{
	struct _URB_CONTROL_TRANSFER_EX	*urb_ctltrans_ex = &urb->UrbControlTransferEx;
	NTSTATUS	status;

	if (!IS_TRANSFER_FLAGS_IN(urb_ctltrans_ex->TransferFlags))
		return STATUS_SUCCESS;
	status = copy_to_transfer_buffer(urb_ctltrans_ex->TransferBuffer, urb_ctltrans_ex->TransferBufferMDL,
		urb_ctltrans_ex->TransferBufferLength, hdr + 1, hdr->u.ret_submit.actual_length);
	if (status == STATUS_SUCCESS)
		urb_ctltrans_ex->TransferBufferLength = hdr->u.ret_submit.actual_length;
	return status;
}
