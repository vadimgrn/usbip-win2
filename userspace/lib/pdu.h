#pragma once

#include "usbip_proto.h"

enum class swap_dir { host2net, net2host };
void byteswap_header(usbip_header &hdr, swap_dir dir) noexcept;

void byteswap_payload(usbip_header &hdr, usbip_dir submit_dir) noexcept;

int get_isoc_descr(usbip_iso_packet_descriptor* &isoc, usbip_header &hdr, usbip_dir submit_dir) noexcept;
size_t get_payload_size(const usbip_header &hdr, usbip_dir submit_dir) noexcept;

inline auto get_total_size(const usbip_header &hdr, usbip_dir submit_dir) noexcept
{
	return sizeof(hdr) + get_payload_size(hdr, submit_dir);
}
