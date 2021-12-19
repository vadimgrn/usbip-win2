#pragma once

#include "usbip_proto.h"

#include <ntddk.h>

void swap_usbip_header(struct usbip_header *hdr);
void swap_usbip_iso_descs(struct usbip_header *hdr);

size_t get_pdu_payload_size(const struct usbip_header *hdr);

__inline size_t get_pdu_size(const struct usbip_header *hdr)
{
	return sizeof(*hdr) + get_pdu_payload_size(hdr);
}
