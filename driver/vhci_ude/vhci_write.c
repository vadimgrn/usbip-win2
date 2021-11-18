#include "vhci_write.h"
#include "vhci_driver.h"
#include "vhci_write.tmh"

#include "usbip_proto.h"
#include "vhci_urbr.h"
#include "vhci_urbr_fetch.h"
#include "vhci_vusb.h"

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

	TraceInfo(TRACE_WRITE, "Enter");

	hdr = get_hdr_from_req_write(req_write);
	if (hdr == NULL) {
		TraceError(TRACE_WRITE, "small write irp");
		status = STATUS_INVALID_PARAMETER;
		goto out;
	}

	urbr = find_sent_urbr(vusb, hdr);
	if (urbr == NULL) {
		// Might have been cancelled before, so return STATUS_SUCCESS
		TraceWarning(TRACE_WRITE, "no urbr: seqnum: %d", hdr->base.seqnum);
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
	TraceInfo(TRACE_WRITE, "Leave: %!STATUS!", status);
}

VOID
io_write(_In_ WDFQUEUE queue, _In_ WDFREQUEST req, _In_ size_t len)
{
	pctx_vusb_t	vusb;
	NTSTATUS	status;

	UNREFERENCED_PARAMETER(queue);

	TraceInfo(TRACE_WRITE, "Enter: len: %u", (ULONG)len);

	vusb = get_vusb_by_req(req);
	if (vusb == NULL) {
		TraceInfo(TRACE_WRITE, "vusb disconnected: port: %u", TO_SAFE_VUSB_FROM_REQ(req)->port);
		status = STATUS_DEVICE_NOT_CONNECTED;
	}
	else {
		write_vusb(vusb, req);
		put_vusb(vusb);
		status = STATUS_SUCCESS;
	}

	WdfRequestCompleteWithInformation(req, status, len);

	TraceInfo(TRACE_WRITE, "Leave");
}
