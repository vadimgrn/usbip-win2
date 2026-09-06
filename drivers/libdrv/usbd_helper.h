/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbip/proto.h>

#include <ntddk.h>
#include <usb.h>

namespace libdrv
{

enum { EndpointStalled = USBD_STATUS_STALL_PID }; // FIXME: for what USBD_STATUS_ENDPOINT_HALTED?

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
int to_linux_status(_In_ USBD_STATUS usbd_status);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
USBD_STATUS to_windows_status_ex(_In_ int usbip_status, _In_ bool isoch);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto to_windows_status(_In_ int usbip_status) { return to_windows_status_ex(usbip_status, false); }

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto to_windows_status_isoch(_In_ int usbip_status) { return to_windows_status_ex(usbip_status, true); }

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG to_windows_flags(_In_ UINT32 transfer_flags, _In_ bool dir_in);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
UINT32 to_linux_flags(_In_ ULONG TransferFlags, _In_ bool dir_in);

constexpr auto IsTransferDirectionIn(_In_ ULONG TransferFlags)
{
	return USBD_TRANSFER_DIRECTION_IN == USBD_TRANSFER_DIRECTION_FLAG(TransferFlags);
}

constexpr auto IsTransferDirectionOut(_In_ ULONG TransferFlags)
{
	return USBD_TRANSFER_DIRECTION_OUT == USBD_TRANSFER_DIRECTION_FLAG(TransferFlags);
}

constexpr auto is_transfer_dir_in(_In_ const USB_DEFAULT_PIPE_SETUP_PACKET &r)
{
	return r.bmRequestType.s.Dir == BMREQUEST_DEVICE_TO_HOST;
}

constexpr auto is_transfer_dir_out(_In_ const USB_DEFAULT_PIPE_SETUP_PACKET &r)
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

} // namespace libdrv
