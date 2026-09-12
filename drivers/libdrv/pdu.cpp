/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "pdu.h"
#include <usbip/proto.h>

#include <wdm.h>
#include <ntstatus.h>
#include <ntintsafe.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void bswap(_Inout_ header_basic &r)
{
        static_assert(sizeof(r.command) == sizeof(unsigned long));
        r.command = RtlUlongByteSwap(r.command);
        r.seqnum = RtlUlongByteSwap(r.seqnum);
        r.devid = RtlUlongByteSwap(r.devid);
        r.direction = RtlUlongByteSwap(r.direction);
        r.ep = RtlUlongByteSwap(r.ep);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void bswap(_Inout_ header_cmd_submit &r) 
{
	static_assert(sizeof(r.transfer_flags) == sizeof(unsigned long));
	r.transfer_flags = RtlUlongByteSwap(r.transfer_flags);
        r.transfer_buffer_length = RtlUlongByteSwap(r.transfer_buffer_length);
        r.start_frame = RtlUlongByteSwap(r.start_frame);
        r.number_of_packets = RtlUlongByteSwap(r.number_of_packets);
        r.interval = RtlUlongByteSwap(r.interval);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void bswap(_Inout_ header_ret_submit &r) 
{
        static_assert(sizeof(r.status) == sizeof(unsigned long));
        r.status = RtlUlongByteSwap(r.status);
        r.actual_length = RtlUlongByteSwap(r.actual_length);
        r.start_frame = RtlUlongByteSwap(r.start_frame);
        r.number_of_packets = RtlUlongByteSwap(r.number_of_packets);
        r.error_count = RtlUlongByteSwap(r.error_count);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void bswap(_Inout_ header_cmd_unlink &r) 
{
	static_assert(sizeof(r.seqnum) == sizeof(unsigned long));
	r.seqnum = RtlUlongByteSwap(r.seqnum);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void bswap(_Inout_ header_ret_unlink &r) 
{
	static_assert(sizeof(r.status) == sizeof(unsigned long));
	r.status = RtlUlongByteSwap(r.status);
}

struct packet_layout
{
        size_t payload;
        size_t number_of_packets;
        bool valid;
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto get_packet_layout(_In_ const header &hdr)
{
        packet_layout result{};

        INT32 number_of_packets;
        INT32 payload_length;

        switch (hdr.command) {
        case CMD_SUBMIT:
                if (!is_valid_direction(hdr.direction)) [[unlikely]] {
                        return result;
                }
                payload_length = hdr.cmd_submit.transfer_buffer_length;
                if (payload_length < 0) [[unlikely]] {
                        return result;
                }
                if (hdr.direction != direction::out) {
                        payload_length = 0;
                }
                number_of_packets = hdr.cmd_submit.number_of_packets;
                break;
        case RET_SUBMIT:
                if (!is_valid_direction(hdr.direction)) [[unlikely]] {
                        return result;
                }
                payload_length = hdr.ret_submit.actual_length;
                if (payload_length < 0) [[unlikely]] {
                        return result;
                }
                if (hdr.direction != direction::in) {
                        payload_length = 0;
                }
                number_of_packets = hdr.ret_submit.number_of_packets;
                break;
        case CMD_UNLINK:
        case RET_UNLINK:
                result.valid = true;
                return result;
        default:
                return result;
        }

        if (number_of_packets != number_of_packets_non_isoch) {
                if (!is_valid_number_of_packets(number_of_packets)) [[unlikely]] {
                        return result;
                }
                result.number_of_packets = static_cast<size_t>(number_of_packets);
        }

        result.payload = static_cast<size_t>(payload_length);
        result.valid = true;
        return result;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void libdrv::byteswap_header(_Inout_ header &hdr, _In_ swap_dir dir) 
{
	if (dir == swap_dir::net2host) {
		bswap(hdr);
	}

	switch (hdr.command) {
	case CMD_SUBMIT:
		bswap(hdr.cmd_submit);
		break;
	case RET_SUBMIT:
		bswap(hdr.ret_submit);
		break;
	case CMD_UNLINK:
		bswap(hdr.cmd_unlink);
		break;
	case RET_UNLINK:
		bswap(hdr.ret_unlink);
		break;
        default:
                NT_ASSERT(!"invalid request_type");
	}

	if (dir == swap_dir::host2net) {
		bswap(hdr);
	}
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void libdrv::byteswap(_Inout_updates_(cnt) iso_packet_descriptor *d, _In_ size_t cnt) 
{
	static_assert(sizeof(d->offset) == sizeof(unsigned long));

	for (const auto end = d + cnt; d != end; ++d) {
		d->offset = RtlUlongByteSwap(d->offset);
		d->length = RtlUlongByteSwap(d->length);
		d->actual_length = RtlUlongByteSwap(d->actual_length);
		d->status = RtlUlongByteSwap(d->status);
	}
}

/*
 * For a server's response, set hdr.base.direction to the value from the corresponding request, 
 * otherwise the result will be incorrect.
 *
 * Server's responses always have zeroes in usbip_header_basic's devid, direction, ep.
 * See: <linux>/Documentation/usb/usbip_protocol.rst, usbip_header_basic.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool libdrv::get_payload_size(_Out_ size_t &result, _In_ const header &hdr)
{
        result = 0;

        auto layout = get_packet_layout(hdr);
        if (!layout.valid) {
                return false;
        }

        size_t isoc_len{};
        if (NT_ERROR(RtlSizeTMult(layout.number_of_packets, sizeof(iso_packet_descriptor), &isoc_len))) {
                return false;
        }

        size_t payload{};
        if (NT_ERROR(RtlSizeTAdd(layout.payload, isoc_len, &payload))) {
                return false;
        }

        result = payload;
        return true;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool libdrv::get_total_size(_Out_ size_t &result, _In_ const header &hdr)
{
        size_t payload{};
        if (!get_payload_size(payload, hdr)) {
                result = 0;
                return false;
        }

        size_t total{};
        if (NT_ERROR(RtlSizeTAdd(sizeof(hdr), payload, &total))) {
                result = 0;
                return false;
        }

        result = total;
        return true;
}
