#include "pdu.h"
#include "usbip_proto.h"

#include <wdm.h>

namespace
{

void swap_cmd_submit(usbip_header_cmd_submit &r)
{
	r.transfer_flags = RtlUlongByteSwap(r.transfer_flags);
	r.transfer_buffer_length = RtlUlongByteSwap(r.transfer_buffer_length);
	r.start_frame = RtlUlongByteSwap(r.start_frame);
	r.number_of_packets = RtlUlongByteSwap(r.number_of_packets);
	r.interval = RtlUlongByteSwap(r.interval);
}

void swap_ret_submit(usbip_header_ret_submit &r)
{
	r.status = RtlUlongByteSwap(r.status);
	r.actual_length = RtlUlongByteSwap(r.actual_length);
	r.start_frame = RtlUlongByteSwap(r.start_frame);
	r.number_of_packets = RtlUlongByteSwap(r.number_of_packets);
	r.error_count = RtlUlongByteSwap(r.error_count);
}

inline void swap_cmd_unlink(usbip_header_cmd_unlink &r)
{
	r.seqnum = RtlUlongByteSwap(r.seqnum);
}

inline void swap_ret_unlink(usbip_header_ret_unlink &r)
{
	r.status = RtlUlongByteSwap(r.status);
}

} // namespace


void swap_usbip_header(usbip_header *hdr)
{
	hdr->base.seqnum = RtlUlongByteSwap(hdr->base.seqnum);
	hdr->base.devid = RtlUlongByteSwap(hdr->base.devid);
	hdr->base.direction = RtlUlongByteSwap(hdr->base.direction);
	hdr->base.ep = RtlUlongByteSwap(hdr->base.ep);

	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		swap_cmd_submit(hdr->u.cmd_submit);
		break;
	case USBIP_RET_SUBMIT:
		swap_ret_submit(hdr->u.ret_submit);
		break;
	case USBIP_CMD_UNLINK:
		swap_cmd_unlink(hdr->u.cmd_unlink);
		break;
	case USBIP_RET_UNLINK:
		swap_ret_unlink(hdr->u.ret_unlink);
		break;
	}

	hdr->base.command = RtlUlongByteSwap(hdr->base.command);
}

void swap_usbip_ds(usbip_header *hdr)
{
	auto cnt = hdr->u.ret_submit.number_of_packets;
	auto d = (usbip_iso_packet_descriptor*)((char*)(hdr + 1) + hdr->u.ret_submit.actual_length);
	
	for (int i = 0; i < cnt; ++i, ++d) {
		d->offset = RtlUlongByteSwap(d->offset);
		d->length = RtlUlongByteSwap(d->length);
		d->actual_length = RtlUlongByteSwap(d->actual_length);
		d->status = RtlUlongByteSwap(d->status);
	}
}

size_t get_pdu_payload_size(const usbip_header *hdr)
{
	NT_ASSERT(hdr);

	int dir_out = hdr->base.direction == USBIP_DIR_OUT;
	size_t len = 0;

	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		len += dir_out ? hdr->u.cmd_submit.transfer_buffer_length : 0;
		len += hdr->u.cmd_submit.number_of_packets*sizeof(usbip_iso_packet_descriptor);
		break;
	case USBIP_RET_SUBMIT:
		len += dir_out ? 0 : hdr->u.ret_submit.actual_length;
		len += hdr->u.ret_submit.number_of_packets*sizeof(usbip_iso_packet_descriptor);
		break;
	case USBIP_CMD_UNLINK:
	case USBIP_RET_UNLINK:
		break;
	}

	return len;
}

size_t get_pdu_size(const usbip_header *hdr)
{
	return sizeof(*hdr) + get_pdu_payload_size(hdr);
}
