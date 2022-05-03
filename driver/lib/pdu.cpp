/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/
#include "pdu.h"
#include "usbip_proto.h"

#include <intrin.h>

#ifdef _KERNEL_MODE
  #include <wdm.h>
  #define USBIP_ASSERT NT_ASSERT
#else
  #include <cassert>
  #define USBIP_ASSERT assert
#endif

namespace
{

void byteswap(usbip_header_basic &r) 
{
        UINT32* v[]{ &r.command, &r.seqnum, &r.devid, &r.direction, &r.ep };
        static_assert(sizeof(*v[0]) == sizeof(unsigned long));

        for (auto val: v) {
		*val = _byteswap_ulong(*val); // RtlUlongByteSwap
	}
}

void byteswap(usbip_header_cmd_submit &r) 
{
	static_assert(sizeof(r.transfer_flags) == sizeof(unsigned long));
	r.transfer_flags = _byteswap_ulong(r.transfer_flags);

        INT32 *v[] {&r.transfer_buffer_length, &r.start_frame, &r.number_of_packets, &r.interval};
        static_assert(sizeof(*v[0]) == sizeof(unsigned long));

	for (auto val: v) {
		*val = _byteswap_ulong(*val);
	}
}

void byteswap(usbip_header_ret_submit &r) 
{
        INT32 *v[] {&r.status, &r.actual_length, &r.start_frame, &r.number_of_packets, &r.error_count};
        static_assert(sizeof(*v[0]) == sizeof(unsigned long));

	for (auto val: v) {
		*val = _byteswap_ulong(*val);
	}
}

inline void byteswap(usbip_header_cmd_unlink &r) 
{
	static_assert(sizeof(r.seqnum) == sizeof(unsigned long));
	r.seqnum = _byteswap_ulong(r.seqnum);
}

inline void byteswap(usbip_header_ret_unlink &r) 
{
	static_assert(sizeof(r.status) == sizeof(unsigned long));
	r.status = _byteswap_ulong(r.status);
}

void byteswap(usbip_iso_packet_descriptor *d, int cnt) 
{
	for (int i = 0; i < cnt; ++i, ++d) {

                UINT32 *v[] {&d->offset, &d->length, &d->actual_length, &d->status};
                static_assert(sizeof(*v[0]) == sizeof(unsigned long));

                for (auto val: v) {
			*val = _byteswap_ulong(*val);
		}
	}
}

} // namespace


void byteswap_header(usbip_header &hdr, swap_dir dir) 
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

void byteswap_payload(usbip_header &hdr) 
{
	usbip_iso_packet_descriptor *isoc{};

	if (auto cnt = get_isoc_descr(isoc, hdr)) {
		byteswap(isoc, cnt);
	}
}

/*
 * Server's responses always have zeroes in usbip_header_basic's devid, direction, ep.
 * See: <linux>/Documentation/usb/usbip_protocol.rst, usbip_header_basic.
 */
int get_isoc_descr(usbip_iso_packet_descriptor* &isoc, usbip_header &hdr) 
{
	auto dir_out = hdr.base.direction == USBIP_DIR_OUT;

	auto buf_end = reinterpret_cast<char*>(&hdr + 1);
	int cnt = 0;

	switch (hdr.base.command) {
	case USBIP_CMD_SUBMIT:
		buf_end += dir_out ? hdr.u.cmd_submit.transfer_buffer_length : 0;
		cnt = hdr.u.cmd_submit.number_of_packets;
		break;
	case USBIP_RET_SUBMIT:
		buf_end += dir_out ? 0 : hdr.u.ret_submit.actual_length; // harmless if direction was not corrected
		cnt = hdr.u.ret_submit.number_of_packets;
		break;
	case USBIP_CMD_UNLINK:
	case USBIP_RET_UNLINK:
		break;
	default:
                USBIP_ASSERT(!"Invalid command, wrong endianness?");
	}

	isoc = reinterpret_cast<usbip_iso_packet_descriptor*>(buf_end);
	return cnt;
}

size_t get_payload_size(const usbip_header &hdr) 
{
	usbip_iso_packet_descriptor *isoc{};
	auto cnt = get_isoc_descr(isoc, const_cast<usbip_header&>(hdr));

	auto buf = reinterpret_cast<const char*>(&hdr + 1);
        USBIP_ASSERT((char*)isoc >= buf);

	return reinterpret_cast<char*>(isoc + cnt) - buf;
}

size_t get_total_size(const usbip_header &hdr) 
{
	return sizeof(hdr) + get_payload_size(hdr);
}
