/*
 * Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "pdu.h"
#include <usbip\proto.h>

#include <intrin.h>
#include <wdm.h>

namespace
{

using namespace usbip;

void byteswap(header_basic &r) 
{
        UINT32* v[]{ &r.command, &r.seqnum, &r.devid, &r.direction, &r.ep };
        static_assert(sizeof(*v[0]) == sizeof(unsigned long));

        for (auto val: v) {
		*val = RtlUlongByteSwap(*val); // _byteswap_ulong
	}
}

void byteswap(header_cmd_submit &r) 
{
	static_assert(sizeof(r.transfer_flags) == sizeof(unsigned long));
	r.transfer_flags = RtlUlongByteSwap(r.transfer_flags);

        INT32 *v[] {&r.transfer_buffer_length, &r.start_frame, &r.number_of_packets, &r.interval};
        static_assert(sizeof(*v[0]) == sizeof(unsigned long));

	for (auto val: v) {
		*val = RtlUlongByteSwap(*val);
	}
}

void byteswap(header_ret_submit &r) 
{
        INT32 *v[] {&r.status, &r.actual_length, &r.start_frame, &r.number_of_packets, &r.error_count};
        static_assert(sizeof(*v[0]) == sizeof(unsigned long));

	for (auto val: v) {
		*val = RtlUlongByteSwap(*val);
	}
}

inline void byteswap(header_cmd_unlink &r) 
{
	static_assert(sizeof(r.seqnum) == sizeof(unsigned long));
	r.seqnum = RtlUlongByteSwap(r.seqnum);
}

inline void byteswap(header_ret_unlink &r) 
{
	static_assert(sizeof(r.status) == sizeof(unsigned long));
	r.status = RtlUlongByteSwap(r.status);
}

} // namespace


void byteswap_header(usbip::header &hdr, swap_dir dir) 
{
	if (dir == swap_dir::net2host) {
		byteswap(hdr);
	}

	switch (hdr.command) {
	case CMD_SUBMIT:
		byteswap(hdr.cmd_submit);
		break;
	case RET_SUBMIT:
		byteswap(hdr.ret_submit);
		break;
	case CMD_UNLINK:
		byteswap(hdr.cmd_unlink);
		break;
	case RET_UNLINK:
		byteswap(hdr.ret_unlink);
		break;
	}

	if (dir == swap_dir::host2net) {
		byteswap(hdr);
	}
}

void byteswap(iso_packet_descriptor *d, size_t cnt) 
{
	for (size_t i = 0; i < cnt; ++i, ++d) {

		UINT32 *v[] {&d->offset, &d->length, &d->actual_length, &d->status};
		static_assert(sizeof(*v[0]) == sizeof(unsigned long));

		for (auto val: v) {
			*val = RtlUlongByteSwap(*val);
		}
	}
}

void byteswap_payload(usbip::header &hdr) 
{
	iso_packet_descriptor *isoc{};

	if (auto cnt = get_isoc_descr(isoc, hdr)) {
		byteswap(isoc, cnt);
	}
}

/*
 * Server's responses always have zeroes in usbip_header_basic's devid, direction, ep.
 * See: <linux>/Documentation/usb/usbip_protocol.rst, usbip_header_basic.
 */
size_t get_isoc_descr(iso_packet_descriptor* &isoc, usbip::header &hdr) 
{
	auto dir_out = hdr.direction == direction::out;

	auto buf_end = reinterpret_cast<char*>(&hdr + 1);
	size_t cnt = 0;

	switch (hdr.command) {
	case CMD_SUBMIT:
		buf_end += dir_out ? hdr.cmd_submit.transfer_buffer_length : 0;
		cnt = hdr.cmd_submit.number_of_packets;
		break;
	case RET_SUBMIT:
		buf_end += dir_out ? 0 : hdr.ret_submit.actual_length; // harmless if direction was not corrected
		cnt = hdr.ret_submit.number_of_packets;
		break;
	case CMD_UNLINK:
	case RET_UNLINK:
		break;
	default:
		NT_ASSERT(!"Invalid command, wrong endianness?");
	}

	isoc = reinterpret_cast<iso_packet_descriptor*>(buf_end);
	return cnt == number_of_packets_non_isoch ? 0 : cnt;
}

size_t get_total_size(const usbip::header &hdr) 
{
	iso_packet_descriptor *isoc{};
	auto cnt = get_isoc_descr(isoc, const_cast<usbip::header&>(hdr));

	return reinterpret_cast<char*>(isoc + cnt) - reinterpret_cast<const char*>(&hdr);
}

size_t get_payload_size(const usbip::header &hdr)
{
	return get_total_size(hdr) - sizeof(hdr);
}
