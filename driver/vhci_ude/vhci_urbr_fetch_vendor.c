#include "vhci_urbr_fetch_vendor.h"
#include "vhci_driver.h"

#include <usbip_proto.h>

#include "vhci_urbr.h"
#include "vhci_urbr_fetch.h"

NTSTATUS
fetch_urbr_vendor_or_class(PURB urb, struct usbip_header *hdr)
{
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *urb_vendor_class = &urb->UrbControlVendorClassRequest;
	NTSTATUS status = STATUS_SUCCESS;

	if (IsTransferDirectionIn(urb_vendor_class->TransferFlags)) {
		status = copy_to_transfer_buffer(urb_vendor_class->TransferBuffer, urb_vendor_class->TransferBufferMDL,
			                         urb_vendor_class->TransferBufferLength, hdr + 1, 
						 hdr->u.ret_submit.actual_length);
		
		if (status == STATUS_SUCCESS) {
			urb_vendor_class->TransferBufferLength = hdr->u.ret_submit.actual_length;
		}
	} else {
		urb_vendor_class->TransferBufferLength = hdr->u.ret_submit.actual_length;
	}

	return status;
}
