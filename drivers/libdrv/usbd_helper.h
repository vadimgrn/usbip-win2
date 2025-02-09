/*
 * Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbip\proto.h>

#include <ntddk.h>
#include <usb.h>

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
	return USBD_TRANSFER_DIRECTION_FLAG(TransferFlags) == USBD_TRANSFER_DIRECTION_IN;
}

constexpr auto IsTransferDirectionOut(ULONG TransferFlags)
{
	return USBD_TRANSFER_DIRECTION_FLAG(TransferFlags) == USBD_TRANSFER_DIRECTION_OUT;
}

constexpr auto is_transfer_dir_in(const USB_DEFAULT_PIPE_SETUP_PACKET &r)
{
	return r.bmRequestType.s.Dir == BMREQUEST_DEVICE_TO_HOST;
}

constexpr auto is_transfer_dir_out(const USB_DEFAULT_PIPE_SETUP_PACKET &r)
{
	return r.bmRequestType.s.Dir == BMREQUEST_HOST_TO_DEVICE;
}

template<typename Transfer>
inline auto& get_setup_packet(Transfer &r)
{
	return reinterpret_cast<USB_DEFAULT_PIPE_SETUP_PACKET&>(r.SetupPacket);
}

template<typename Transfer>
inline auto& get_setup_packet(const Transfer &r)
{
	return reinterpret_cast<const USB_DEFAULT_PIPE_SETUP_PACKET&>(r.SetupPacket);
}

inline bool operator == (
	_In_ const USB_DEFAULT_PIPE_SETUP_PACKET &a, _In_ const USB_DEFAULT_PIPE_SETUP_PACKET &b)
{
	return RtlEqualMemory(&a, &b, sizeof(a));
}

inline auto operator != (
	_In_ const USB_DEFAULT_PIPE_SETUP_PACKET &a, _In_ const USB_DEFAULT_PIPE_SETUP_PACKET &b)
{
	return !(a == b);
}

template<typename Transfer>
inline auto is_transfer_dir_in(const Transfer &r)
{
	auto &pkt = get_setup_packet(r);
	return is_transfer_dir_in(pkt);
}

template<typename Transfer>
inline auto is_transfer_dir_out(const Transfer &r)
{
	auto &pkt = get_setup_packet(r);
	return is_transfer_dir_out(pkt);
}

constexpr auto is_transfer_dir_in(const usbip::header &h)
{
	return h.direction == usbip::direction::in;
}

constexpr auto is_transfer_dir_out(const usbip::header &h)
{
	return h.direction == usbip::direction::out;
}

constexpr auto is_isoch(_In_ const URB &urb)
{
	auto f = urb.UrbHeader.Function;
	return  f == URB_FUNCTION_ISOCH_TRANSFER || 
		f == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL;
}
