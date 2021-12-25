#include "pdu.h"
#include <stdbool.h>

static void
swap_cmd_submit(struct usbip_header_cmd_submit *cmd_submit)
{
	cmd_submit->transfer_flags = RtlUlongByteSwap(cmd_submit->transfer_flags);
	cmd_submit->transfer_buffer_length = RtlUlongByteSwap(cmd_submit->transfer_buffer_length);
	cmd_submit->start_frame = RtlUlongByteSwap(cmd_submit->start_frame);
	cmd_submit->number_of_packets = RtlUlongByteSwap(cmd_submit->number_of_packets);
	cmd_submit->interval = RtlUlongByteSwap(cmd_submit->interval);
}

static void
swap_ret_submit(struct usbip_header_ret_submit *ret_submit)
{
	ret_submit->status = RtlUlongByteSwap(ret_submit->status);
	ret_submit->actual_length = RtlUlongByteSwap(ret_submit->actual_length);
	ret_submit->start_frame = RtlUlongByteSwap(ret_submit->start_frame);
	ret_submit->number_of_packets = RtlUlongByteSwap(ret_submit->number_of_packets);
	ret_submit->error_count = RtlUlongByteSwap(ret_submit->error_count);
}

static void
swap_cmd_unlink(struct usbip_header_cmd_unlink *cmd_unlink)
{
	cmd_unlink->seqnum = RtlUlongByteSwap(cmd_unlink->seqnum);
}

static void
swap_ret_unlink(struct usbip_header_ret_unlink *ret_unlink)
{
	ret_unlink->status = RtlUlongByteSwap(ret_unlink->status);
}

void
swap_usbip_header(struct usbip_header *hdr)
{
	hdr->base.seqnum = RtlUlongByteSwap(hdr->base.seqnum);
	hdr->base.devid = RtlUlongByteSwap(hdr->base.devid);
	hdr->base.direction = RtlUlongByteSwap(hdr->base.direction);
	hdr->base.ep = RtlUlongByteSwap(hdr->base.ep);

	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		swap_cmd_submit(&hdr->u.cmd_submit);
		break;
	case USBIP_RET_SUBMIT:
		swap_ret_submit(&hdr->u.ret_submit);
		break;
	case USBIP_CMD_UNLINK:
		swap_cmd_unlink(&hdr->u.cmd_unlink);
		break;
	case USBIP_RET_UNLINK:
		swap_ret_unlink(&hdr->u.ret_unlink);
		break;
	default:
		break;
	}

	hdr->base.command = RtlUlongByteSwap(hdr->base.command);
}

void
swap_usbip_iso_descs(struct usbip_header *hdr)
{
	struct usbip_iso_packet_descriptor	*iso_desc;
	int	n_pkts;
	int	i;

	n_pkts = hdr->u.ret_submit.number_of_packets;
	iso_desc = (struct usbip_iso_packet_descriptor *)((char *)(hdr + 1) + hdr->u.ret_submit.actual_length);
	for (i = 0; i < n_pkts; i++) {
		iso_desc->offset = RtlUlongByteSwap(iso_desc->offset);
		iso_desc->length = RtlUlongByteSwap(iso_desc->length);
		iso_desc->actual_length = RtlUlongByteSwap(iso_desc->actual_length);
		iso_desc->status = RtlUlongByteSwap(iso_desc->status);
		iso_desc++;
	}
}

size_t get_pdu_payload_size(const struct usbip_header *hdr)
{
	NT_ASSERT(hdr);

	bool dir_out = hdr->base.direction == USBIP_DIR_OUT;
	size_t len = 0;

	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		len += dir_out ? hdr->u.cmd_submit.transfer_buffer_length : 0;
		len += hdr->u.cmd_submit.number_of_packets*sizeof(struct usbip_iso_packet_descriptor);
		break;
	case USBIP_RET_SUBMIT:
		len += dir_out ? 0 : hdr->u.ret_submit.actual_length;
		len += hdr->u.ret_submit.number_of_packets*sizeof(struct usbip_iso_packet_descriptor);
		break;
	case USBIP_CMD_UNLINK:
	case USBIP_RET_UNLINK:
		break;
	}

	return len;
}