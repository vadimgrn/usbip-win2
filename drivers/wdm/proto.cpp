#include "proto.h"
#include "trace.h"
#include "proto.tmh"

#include <libdrv\usbd_helper.h>

#include "devconf.h"
#include "dev.h"

namespace
{

_IRQL_requires_max_(DISPATCH_LEVEL)
auto fix_transfer_flags(ULONG TransferFlags, USBD_PIPE_HANDLE PipeHandle)
{
	NT_ASSERT(PipeHandle);

	bool out = is_endpoint_direction_out(PipeHandle);

	if (IsTransferDirectionOut(TransferFlags) == out) {
		return TransferFlags;
	}

	TraceDbg("Fix direction in TransferFlags(%#Ix), PipeHandle(%#Ix)", TransferFlags, (uintptr_t)PipeHandle);

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
NTSTATUS set_cmd_submit_usbip_header(
	_In_ vpdo_dev_t &vpdo, _Out_ usbip_header &hdr, _In_ USBD_PIPE_HANDLE PipeHandle, ULONG TransferFlags, _In_ ULONG TransferBufferLength)
{
	bool ep0 = TransferFlags & USBD_DEFAULT_PIPE_TRANSFER;
	if (ep0 == !!PipeHandle) {
		Trace(TRACE_LEVEL_ERROR, "Inconsistency between TransferFlags(USBD_DEFAULT_PIPE_TRANSFER) and PipeHandle(%#Ix)", 
					(uintptr_t)PipeHandle);

		return STATUS_INVALID_PARAMETER;
	}
	
	if (PipeHandle) {
		TransferFlags = fix_transfer_flags(TransferFlags, PipeHandle);
	}

	bool dir_in = IsTransferDirectionIn(TransferFlags); // many URBs don't have PipeHandle

	if (auto r = &hdr.base) {
		r->command = USBIP_CMD_SUBMIT;
		r->seqnum = next_seqnum(vpdo, dir_in);
		r->devid = vpdo.devid;
		r->direction = dir_in ? USBIP_DIR_IN : USBIP_DIR_OUT;
		r->ep = get_endpoint_number(PipeHandle);
	}

	if (auto r = &hdr.u.cmd_submit) {
		r->transfer_flags = to_linux_flags(TransferFlags, dir_in);
		r->transfer_buffer_length = TransferBufferLength;
		r->start_frame = 0;
		r->number_of_packets = number_of_packets_non_isoch;
		r->interval = get_endpoint_interval(PipeHandle);
		RtlZeroMemory(r->setup, sizeof(r->setup));
	}

	return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void set_cmd_unlink_usbip_header(_In_ vpdo_dev_t &vpdo, _Out_ usbip_header &hdr, _In_ seqnum_t seqnum_unlink)
{
	auto &r = hdr.base;

	r.command = USBIP_CMD_UNLINK;
	r.devid = vpdo.devid;
	r.direction = USBIP_DIR_OUT;
	r.seqnum = next_seqnum(vpdo, r.direction);
	r.ep = 0;

	NT_ASSERT(is_valid_seqnum(seqnum_unlink));
	hdr.u.cmd_unlink.seqnum = seqnum_unlink;
}
