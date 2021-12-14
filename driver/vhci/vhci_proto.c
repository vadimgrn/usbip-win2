#include "vhci_proto.h"
#include "trace.h"
#include "vhci_proto.tmh"

#include "usbreq.h"
#include "usbip_proto.h"
#include "usbd_helper.h"

NTSTATUS set_cmd_submit_usbip_header(
	struct usbip_header* h, unsigned long seqnum, UINT32 devid,
	USBD_PIPE_HANDLE PipeHandle, ULONG TransferFlags, ULONG TransferBufferLength)
{
	bool def_pipe = TransferFlags & USBD_DEFAULT_PIPE_TRANSFER;
	if (def_pipe == !!PipeHandle) {
		TraceError(TRACE_READ, "Inconsistency between TransferFlags(USBD_DEFAULT_PIPE_TRANSFER) and PipeHandle(%#Ix)", 
					(uintptr_t)PipeHandle);

		return STATUS_INVALID_PARAMETER;
	}
	
	bool dir_in = IsTransferDirectionIn(TransferFlags);

	if (PipeHandle && is_endpoint_direction_in(PipeHandle) != dir_in) {
		TraceError(TRACE_READ, "Inconsistency between transfer direction in TransferFlags(%#Ix) and PipeHandle(%#Ix)", 
					TransferFlags, (uintptr_t)PipeHandle);

			return STATUS_INVALID_PARAMETER;
	}

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

void
set_cmd_unlink_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid, unsigned long seqnum_unlink)
{
	h->base.command = USBIP_CMD_UNLINK;
	h->base.seqnum = seqnum;
	h->base.devid = devid;
	h->base.direction = USBIP_DIR_OUT;
	h->base.ep = 0;
	h->u.cmd_unlink.seqnum = seqnum_unlink;
}
