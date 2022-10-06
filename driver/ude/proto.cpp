#include "proto.h"
#include "trace.h"
#include "proto.tmh"

#include "context.h"

#include <libdrv\usbd_helper.h>
#include <libdrv\ch9.h>

namespace
{

_IRQL_requires_max_(DISPATCH_LEVEL)
auto fix_transfer_flags(_In_ ULONG TransferFlags, _In_ const USB_ENDPOINT_DESCRIPTOR &epd)
{
	auto out = usb_endpoint_dir_out(epd);

	if (IsTransferDirectionOut(TransferFlags) == out) {
		return TransferFlags;
	}

	TraceDbg("Fix direction in TransferFlags(%#lx) to %s", TransferFlags, out ? "OUT" : "IN");

	const ULONG in_flags = USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN;

	if (out) {
		TransferFlags &= ~in_flags;
	} else {
		TransferFlags |= in_flags;
	}

	NT_ASSERT(IsTransferDirectionOut(TransferFlags) == out);
	return TransferFlags;
}

} // namespace


/*
 * Direction in TransferFlags can be invalid for bulk transfer at least.
 * Always use direction from PipeHandle if URB has one.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::set_cmd_submit_usbip_header(
	_Out_ usbip_header &hdr, _Inout_ device_ctx &dev, _In_ const endpoint_ctx &endp,
	_In_ ULONG TransferFlags, _In_ ULONG TransferBufferLength)
{
	auto &epd = endp.descriptor;
	auto ep0 = epd.bEndpointAddress == USB_DEFAULT_ENDPOINT_ADDRESS; // default control pipe

	if ((TransferFlags & USBD_DEFAULT_PIPE_TRANSFER) && !ep0) {
		Trace(TRACE_LEVEL_ERROR, "Inconsistency between TransferFlags(USBD_DEFAULT_PIPE_TRANSFER) and "
			                 "bEndpointAddress(%#x)", epd.bEndpointAddress);

		return STATUS_INVALID_PARAMETER;
	}
	
	if (!ep0) { // ep0 is bidirectional
		TransferFlags = fix_transfer_flags(TransferFlags, epd);
	}

	auto dir_in = IsTransferDirectionIn(TransferFlags); // many URBs don't have PipeHandle

	if (auto r = &hdr.base) {
		r->command = USBIP_CMD_SUBMIT;
		r->seqnum = next_seqnum(dev, dir_in);
		r->devid = dev.devid();
		r->direction = dir_in ? USBIP_DIR_IN : USBIP_DIR_OUT;
		r->ep = usb_endpoint_num(epd);
	}

	if (auto r = &hdr.u.cmd_submit) {
		r->transfer_flags = to_linux_flags(TransferFlags, dir_in);
		r->transfer_buffer_length = TransferBufferLength;
		r->start_frame = 0;
		r->number_of_packets = 0; // FIXME: -1 if non-isoch
		r->interval = epd.bInterval;
		RtlZeroMemory(r->setup, sizeof(r->setup));
	}

	return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::set_cmd_unlink_usbip_header(
	_Out_ usbip_header &hdr, _Inout_ device_ctx &dev, _In_ seqnum_t seqnum_unlink)
{
	auto &r = hdr.base;

	r.command = USBIP_CMD_UNLINK;
	r.devid = dev.devid();
	r.direction = USBIP_DIR_OUT;
	r.seqnum = next_seqnum(dev, r.direction);
	r.ep = 0;

	NT_ASSERT(is_valid_seqnum(seqnum_unlink));
	hdr.u.cmd_unlink.seqnum = seqnum_unlink;
}
