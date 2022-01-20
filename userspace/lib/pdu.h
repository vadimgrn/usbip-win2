#pragma once

struct usbip_header;
struct usbip_iso_packet_descriptor;

enum class swap_dir { host2net, net2host };
void byteswap_header(usbip_header &hdr, swap_dir dir) noexcept;

void byteswap_payload(usbip_header &hdr) noexcept;

int get_isoc_descr(usbip_header &hdr, usbip_iso_packet_descriptor* &isoc) noexcept;

size_t get_payload_size(const usbip_header &hdr) noexcept;
size_t get_total_size(const usbip_header &hdr) noexcept;
