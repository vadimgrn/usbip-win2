#include "debug.h"
#include "usbip_proto.h"
//#include "usbip_vhci_api.h"

#include <cassert>
#include <cstdio>


#include <ntdef.h>
#include <usb.h>

namespace
{

constexpr auto snprintf_ok(int result, size_t size) noexcept
{
	return result > 0 && static_cast<size_t>(result) < size;
}

constexpr auto bmrequest_dir_str(BM_REQUEST_TYPE r) noexcept
{
	return r.s.Dir == BMREQUEST_HOST_TO_DEVICE ? "OUT" : "IN";
}

auto bmrequest_type_str(BM_REQUEST_TYPE r) noexcept
{
	static const char* v[] = { "STANDARD", "CLASS", "VENDOR", "BMREQUEST_3" };
	assert(r.s.Type < sizeof(v)/sizeof(*v));
	return v[r.s.Type];
}

auto bmrequest_recipient_str(BM_REQUEST_TYPE r) noexcept
{
	static const char* v[] = { "DEVICE", "INTERFACE", "ENDPOINT", "OTHER" };
	assert(r.s.Recipient < sizeof(v)/sizeof(*v));
	return v[r.s.Recipient];
}

auto brequest_str(unsigned char bRequest) noexcept
{
	switch (bRequest) {
	case USB_REQUEST_GET_STATUS: return "GET_STATUS";
	case USB_REQUEST_CLEAR_FEATURE: return "CLEAR_FEATURE";
	case USB_REQUEST_SET_FEATURE: return "SET_FEATURE";
	case USB_REQUEST_SET_ADDRESS: return "SET_ADDRESS";
	case USB_REQUEST_GET_DESCRIPTOR: return "GET_DESCRIPTOR";
	case USB_REQUEST_SET_DESCRIPTOR: return "SET_DESCRIPTOR";
	case USB_REQUEST_GET_CONFIGURATION: return "GET_CONFIGURATION";
	case USB_REQUEST_SET_CONFIGURATION: return "SET_CONFIGURATION";
	case USB_REQUEST_GET_INTERFACE: return "GET_INTERFACE";
	case USB_REQUEST_SET_INTERFACE: return "SET_INTERFACE";
	case USB_REQUEST_SYNC_FRAME: return "SYNC_FRAME";
	case USB_REQUEST_GET_FIRMWARE_STATUS: return "GET_FIRMWARE_STATUS";
	case USB_REQUEST_SET_FIRMWARE_STATUS: return "SET_FIRMWARE_STATUS";
	case USB_REQUEST_SET_SEL: return "SET_SEL";
	case USB_REQUEST_ISOCH_DELAY: return "ISOCH_DELAY";
	}

	return "USB_REQUEST_?";
}

auto usbd_pipe_type_str(USBD_PIPE_TYPE t) noexcept
{
	static const char* v[] = { "Ctrl", "Isoch", "Bulk", "Intr" };
	assert(t < sizeof(v)/sizeof(*v));
	return v[t];
}

void print(char *buf, size_t len, const usbip_header_cmd_submit &r) noexcept
{
	auto ret = snprintf(buf, len, 
			"cmd_submit: flags %#x, length %d, start_frame %d, isoc[%d], interval %d, ",
			r.transfer_flags, 
			r.transfer_buffer_length, 
			r.start_frame, 
			r.number_of_packets, 
			r.interval);

	if (snprintf_ok(ret, len)) {
		print_usb_setup(buf, len, r.setup);
	}
}

inline void print(char *buf, size_t len, const usbip_header_ret_submit &r) noexcept
{
	snprintf(buf, len, 
		"ret_submit: status %d, actual_length %d, start_frame %d, isoc[%d], error_count %d", 
		r.status,
		r.actual_length,
		r.start_frame,
		r.number_of_packets,
		r.error_count);
}

} // namespace


const char *print(char *buf, size_t len, const usbip_header &hdr) noexcept
{
	auto result = buf;
	auto &b = hdr.base;

	auto ret = snprintf(buf, len, "{seqnum %u, devid %#x, %s[%u]}, ",
					b.seqnum, b.devid, 
					b.direction == USBIP_DIR_OUT ? "out" : "in",
					b.ep);

	if (!snprintf_ok(ret, len)) {
		return "print(usbip_header) error";
	}

	buf += ret;
	len -= ret;

	switch (b.command) {
	case USBIP_CMD_SUBMIT:
		print(buf, len, hdr.u.cmd_submit);
		break;
	case USBIP_RET_SUBMIT:
		print(buf, len, hdr.u.ret_submit);
		break;
	case USBIP_CMD_UNLINK:
		snprintf(buf, len, "cmd_unlink: seqnum %u", hdr.u.cmd_unlink.seqnum);
		break;
	case USBIP_RET_UNLINK:
		snprintf(buf, len, "ret_unlink: status %d", hdr.u.ret_unlink.status);
		break;
	default:
		snprintf(buf, len, "command %u", b.command);
	}

	return result;
}

const char *print_usb_setup(char *buf, size_t len, const void *packet) noexcept
{
	auto &r = *static_cast<const USB_DEFAULT_PIPE_SETUP_PACKET*>(packet);

	auto ret = snprintf(buf, len, 
			"{%s|%s|%s, %s(%#02hhx), wValue %#04hx, wIndex %#04hx, wLength %#04hx(%d)}",
			bmrequest_dir_str(r.bmRequestType),
			bmrequest_type_str(r.bmRequestType),
			bmrequest_recipient_str(r.bmRequestType),
			brequest_str(r.bRequest),
			r.bRequest,
			r.wValue.W,
			r.wIndex.W, 
			r.wLength,
			r.wLength);

	return snprintf_ok(ret, len) ? buf : "print(USB_DEFAULT_PIPE_SETUP_PACKET) error";
}
