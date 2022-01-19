#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "usbip_proto.h"

void swap_usbip_header(struct usbip_header *hdr);
void swap_usbip_iso_descs(struct usbip_header *hdr);

__inline size_t get_pdu_payload_size(const struct usbip_header *hdr)
{
//	NT_ASSERT(hdr);

	int dir_out = hdr->base.direction == USBIP_DIR_OUT;
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

__inline size_t get_pdu_size(const struct usbip_header *hdr)
{
	return sizeof(*hdr) + get_pdu_payload_size(hdr);
}

#ifdef __cplusplus
}
#endif
