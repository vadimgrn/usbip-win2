#include "proto.h"
#include "trace.h"
#include "proto.tmh"

#include "usbd_helper.h"
#include "devconf.h"

namespace
{

ULONG fix_transfer_flags(ULONG TransferFlags, USBD_PIPE_HANDLE PipeHandle)
{
	NT_ASSERT(PipeHandle);

	bool out = is_endpoint_direction_out(PipeHandle);

	if (IsTransferDirectionOut(TransferFlags) == out) {
		return TransferFlags;
	}

	Trace(TRACE_LEVEL_VERBOSE, "Fix direction in TransferFlags(%#Ix), PipeHandle(%#Ix)", TransferFlags, (uintptr_t)PipeHandle);

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
NTSTATUS set_cmd_submit_usbip_header(
	struct usbip_header* h, seqnum_t seqnum, UINT32 devid,
	USBD_PIPE_HANDLE PipeHandle, ULONG TransferFlags, ULONG TransferBufferLength)
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

	struct usbip_header_basic *base = &h->base;
	base->command = USBIP_CMD_SUBMIT;
	base->seqnum = seqnum;
	base->devid = devid;
	base->direction = dir_in ? USBIP_DIR_IN : USBIP_DIR_OUT;
	base->ep = get_endpoint_number(PipeHandle);

	struct usbip_header_cmd_submit *cmd = &h->u.cmd_submit;
	cmd->transfer_flags = to_linux_flags(TransferFlags, dir_in);
	cmd->transfer_buffer_length = TransferBufferLength;
	cmd->start_frame = 0;
	cmd->number_of_packets = 0;
	cmd->interval = get_endpoint_interval(PipeHandle);
	RtlZeroMemory(cmd->setup, sizeof(cmd->setup));

	return STATUS_SUCCESS;
}

void set_cmd_unlink_usbip_header(struct usbip_header *h, seqnum_t seqnum, unsigned int devid, seqnum_t seqnum_unlink)
{
	h->base.command = USBIP_CMD_UNLINK;
	h->base.seqnum = seqnum;
	h->base.devid = devid;
	h->base.direction = USBIP_DIR_OUT;
	h->base.ep = 0;
	h->u.cmd_unlink.seqnum = seqnum_unlink;
}
