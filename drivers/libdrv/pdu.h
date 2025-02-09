/*
 * Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

namespace usbip
{
        struct header;
        struct iso_packet_descriptor;
};

enum class swap_dir { host2net, net2host };

void byteswap_header(usbip::header &hdr, swap_dir dir);
void byteswap_payload(usbip::header &hdr);
void byteswap(usbip::iso_packet_descriptor *d, size_t cnt);

/*
 * For a server's response, set hdr.base.direction to the value from the corresponding request, 
 * otherwise the result will be incorrect.
 */
size_t get_isoc_descr(usbip::iso_packet_descriptor* &isoc, usbip::header &hdr);

size_t get_payload_size(const usbip::header &hdr);
size_t get_total_size(const usbip::header &hdr);
