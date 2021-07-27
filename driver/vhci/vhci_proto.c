#include "vhci_proto.h"
#include "usbreq.h"
#include "usbip_proto.h"
#include "usbd_helper.h"

void set_cmd_submit_usbip_header(
	struct usbip_header* h, unsigned long seqnum, UINT32 devid, bool dir_in, 
	USBD_PIPE_HANDLE pipe, ULONG TransferFlags, ULONG TransferBufferLength)
{
	struct usbip_header_basic *base = &h->base;
	base->command = USBIP_CMD_SUBMIT;
	base->seqnum = seqnum;
	base->devid = devid;
	base->direction = dir_in ? USBIP_DIR_IN : USBIP_DIR_OUT;
	base->ep = get_endpoint_number(pipe);

	struct usbip_header_cmd_submit *cmd = &h->u.cmd_submit;
	cmd->transfer_flags = to_linux_flags(TransferFlags);
	cmd->transfer_buffer_length = TransferBufferLength;
	cmd->start_frame = 0;
	cmd->number_of_packets = 0;
	cmd->interval = get_endpoint_interval(pipe);
	RtlZeroMemory(cmd->setup, sizeof(cmd->setup));
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
