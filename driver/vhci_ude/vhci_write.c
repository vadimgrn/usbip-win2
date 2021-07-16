#include "vhci_driver.h"
#include "vhci_write.tmh"

#include "usbip_proto.h"
#include "vhci_urbr.h"

extern purb_req_t
find_sent_urbr(pctx_vusb_t vusb, struct usbip_header *hdr);

extern NTSTATUS
fetch_urbr(purb_req_t urbr, struct usbip_header *hdr);

static struct usbip_header *
get_hdr_from_req_write(WDFREQUEST req_write)
{
	struct usbip_header *hdr;
	size_t		len;
	NTSTATUS	status;

	status = WdfRequestRetrieveInputBuffer(req_write, sizeof(struct usbip_header), &hdr, &len);
	if (NT_ERROR(status)) {
		WdfRequestSetInformation(req_write, 0);
		return NULL;
	}

	WdfRequestSetInformation(req_write, len);
	return hdr;
}

static VOID
write_vusb(pctx_vusb_t vusb, WDFREQUEST req_write)
{
	struct usbip_header *hdr;
	purb_req_t	urbr;
	NTSTATUS	status;

	TRD(WRITE, "Enter");

	hdr = get_hdr_from_req_write(req_write);
	if (hdr == NULL) {
		TRE(WRITE, "small write irp\n");
		status = STATUS_INVALID_PARAMETER;
		goto out;
	}

	urbr = find_sent_urbr(vusb, hdr);
	if (urbr == NULL) {
		// Might have been cancelled before, so return STATUS_SUCCESS
		TRW(WRITE, "no urbr: seqnum: %d", hdr->base.seqnum);
		status = STATUS_SUCCESS;
		goto out;
	}

	status = fetch_urbr(urbr, hdr);

	WdfSpinLockAcquire(vusb->spin_lock);
	if (unmark_cancelable_urbr(urbr)) {
		WdfSpinLockRelease(vusb->spin_lock);
		complete_urbr(urbr, status);
	}
	else {
		WdfSpinLockRelease(vusb->spin_lock);
	}
out:
	TRD(WRITE, "Leave: %!STATUS!", status);
}

VOID
io_write(_In_ WDFQUEUE queue, _In_ WDFREQUEST req, _In_ size_t len)
{
	pctx_vusb_t	vusb;
	NTSTATUS	status;

	UNREFERENCED_PARAMETER(queue);

	TRD(WRITE, "Enter: len: %u", (ULONG)len);

	vusb = get_vusb_by_req(req);
	if (vusb == NULL) {
		TRD(WRITE, "vusb disconnected: port: %u", TO_SAFE_VUSB_FROM_REQ(req)->port);
		status = STATUS_DEVICE_NOT_CONNECTED;
	}
	else {
		write_vusb(vusb, req);
		put_vusb(vusb);
		status = STATUS_SUCCESS;
	}

	WdfRequestCompleteWithInformation(req, status, len);

	TRD(WRITE, "Leave");
}
