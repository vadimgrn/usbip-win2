#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct usbip_header;

void swap_usbip_header(struct usbip_header *hdr);
void swap_usbip_iso_descs(struct usbip_header *hdr);

size_t get_pdu_payload_size(const struct usbip_header *hdr);
size_t get_pdu_size(const struct usbip_header *hdr);

#ifdef __cplusplus
}
#endif
