#pragma once

struct usbip_header;
struct usbip_iso_packet_descriptor;

enum class swap_dir { host2net, net2host };
void byteswap_header(usbip_header &hdr, swap_dir dir) noexcept;

void byteswap_payload(usbip_header &hdr) noexcept;

/*
 * For the server response, set hdr.base.direction to the value from the corresponding request, 
 * otherwise the result will be incorrect.
 */
int get_isoc_descr(usbip_iso_packet_descriptor* &isoc, usbip_header &hdr) noexcept;
size_t get_payload_size(const usbip_header &hdr) noexcept;
size_t get_total_size(const usbip_header &hdr) noexcept;
