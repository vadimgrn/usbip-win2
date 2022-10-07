#pragma once

#include <usbip\proto.h>
#include <usbip\ch9.h>

#include <ntddk.h>
#include <usbdi.h>

struct usbip_iso_packet_descriptor;

enum { EndpointStalled = USBD_STATUS_STALL_PID }; // FIXME: for what USBD_STATUS_ENDPOINT_HALTED?

int to_linux_status(USBD_STATUS usbd_status);
USBD_STATUS to_windows_status_ex(int usbip_status, bool isoch);

inline auto to_windows_status(int usbip_status) { return to_windows_status_ex(usbip_status, false); }
inline auto to_windows_status_isoch(int usbip_status) { return to_windows_status_ex(usbip_status, true); }

ULONG to_windows_flags(UINT32 transfer_flags, bool dir_in);
UINT32 to_linux_flags(ULONG TransferFlags, bool dir_in);

constexpr auto IsTransferDirectionIn(ULONG TransferFlags)
{
	return USBD_TRANSFER_DIRECTION(TransferFlags) == USBD_TRANSFER_DIRECTION_IN;
}

constexpr auto IsTransferDirectionOut(ULONG TransferFlags)
{
	return USBD_TRANSFER_DIRECTION(TransferFlags) == USBD_TRANSFER_DIRECTION_OUT;
}

inline bool is_transfer_dir_in(const _URB_CONTROL_TRANSFER &r)
{
	auto &pkt = *reinterpret_cast<const USB_DEFAULT_PIPE_SETUP_PACKET*>(r.SetupPacket);

	static_assert(USB_DIR_IN);
	return pkt.bmRequestType.B & USB_DIR_IN; // C: bmRequestType.Dir, C++: bmRequestType.s.Dir
}

inline auto is_transfer_dir_out(const _URB_CONTROL_TRANSFER &r)
{
	static_assert(!USB_DIR_OUT);
	return !is_transfer_dir_in(r);
}

constexpr auto is_transfer_direction_in(const usbip_header &h)
{
	return h.base.direction == USBIP_DIR_IN;
}

constexpr auto is_transfer_direction_out(const usbip_header &h)
{
	return h.base.direction == USBIP_DIR_OUT;
}

constexpr auto is_isoch(_In_ const URB &urb)
{
	auto f = urb.UrbHeader.Function;
	return  f == URB_FUNCTION_ISOCH_TRANSFER || 
		f == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL;
}
