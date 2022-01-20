#include "pdu.h"
#include "usbip_proto.h"

#include <cassert>
#include <stdlib.h>
#include <utility>

namespace
{

void byteswap(usbip_iso_packet_descriptor *d, int cnt) noexcept
{
	for (int i = 0; i < cnt; ++i, ++d) {
		for (auto val: {&d->offset, &d->length, &d->actual_length, &d->status}) {
			static_assert(sizeof(*val) == sizeof(unsigned long));
			*val = _byteswap_ulong(*val);
		}
	}
}

void byteswap(usbip_header_basic &r) noexcept
{
	for (auto val: {&r.command, &r.seqnum, &r.devid, &r.direction, &r.ep}) {
		static_assert(sizeof(*val) == sizeof(unsigned long));
		*val = _byteswap_ulong(*val);
	}
}

void byteswap(usbip_header_cmd_submit &r) noexcept
{
	static_assert(sizeof(r.transfer_flags) == sizeof(unsigned long));
	r.transfer_flags = _byteswap_ulong(r.transfer_flags);

	for (auto val: {&r.transfer_buffer_length, &r.start_frame, &r.number_of_packets, &r.interval}) {
		static_assert(sizeof(*val) == sizeof(unsigned long));
		*val = _byteswap_ulong(*val);
	}
}

void byteswap(usbip_header_ret_submit &r) noexcept
{
	for (auto val: {&r.status, &r.actual_length, &r.start_frame, &r.number_of_packets, &r.error_count}) {
		static_assert(sizeof(*val) == sizeof(unsigned long));
		*val = _byteswap_ulong(*val);
	}
}

inline void byteswap(usbip_header_cmd_unlink &r) noexcept
{
	r.seqnum = _byteswap_ulong(r.seqnum);
	static_assert(sizeof(r.seqnum) == sizeof(unsigned long));
}

inline void byteswap(usbip_header_ret_unlink &r) noexcept
{
	r.status = _byteswap_ulong(r.status);
	static_assert(sizeof(r.status) == sizeof(unsigned long));
}

} // namespace


void byteswap_header(usbip_header &hdr, swap_dir dir) noexcept
{
	if (dir == swap_dir::net2host) {
		byteswap(hdr.base);
	}

	switch (hdr.base.command) {
	case USBIP_CMD_SUBMIT:
		byteswap(hdr.u.cmd_submit);
		break;
	case USBIP_RET_SUBMIT:
		byteswap(hdr.u.ret_submit);
		break;
	case USBIP_CMD_UNLINK:
		byteswap(hdr.u.cmd_unlink);
		break;
	case USBIP_RET_UNLINK:
		byteswap(hdr.u.ret_unlink);
		break;
	}

	if (dir == swap_dir::host2net) {
		byteswap(hdr.base);
	}
}

void byteswap_payload(usbip_header &hdr) noexcept
{
	usbip_iso_packet_descriptor *isoc{};

	if (auto cnt = get_isoc_descr(hdr, isoc)) {
		byteswap(isoc, cnt);
	}
}

int get_isoc_descr(usbip_header &hdr, usbip_iso_packet_descriptor* &isoc) noexcept
{
	bool dir_out = hdr.base.direction == USBIP_DIR_OUT;

	auto buf_end = reinterpret_cast<char*>(&hdr + 1);
	int cnt = 0;

	switch (hdr.base.command) {
	case USBIP_CMD_SUBMIT:
		buf_end += dir_out ? hdr.u.cmd_submit.transfer_buffer_length : 0;
		cnt = hdr.u.cmd_submit.number_of_packets;
		break;
	case USBIP_RET_SUBMIT:
		buf_end += dir_out ? 0 : hdr.u.ret_submit.actual_length;
		cnt = hdr.u.ret_submit.number_of_packets;
		break;
	case USBIP_CMD_UNLINK:
	case USBIP_RET_UNLINK:
		break;
	default:
		assert(!"Invalid command, wrong endianness?");
	}

	isoc = reinterpret_cast<usbip_iso_packet_descriptor*>(buf_end);
	return cnt;
}

size_t get_payload_size(const usbip_header &hdr) noexcept
{
	usbip_iso_packet_descriptor *isoc{};
	auto cnt = get_isoc_descr(const_cast<usbip_header&>(hdr), isoc);

	auto buf = reinterpret_cast<const char*>(&hdr + 1);
	assert((char*)isoc >= buf);

	return reinterpret_cast<char*>(isoc + cnt) - buf;
}

size_t get_total_size(const usbip_header &hdr) noexcept
{
	return sizeof(hdr) + get_payload_size(hdr);
}
