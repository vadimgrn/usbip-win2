/*
 * Copyright (C) 2023 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\ch9.h>
#include <libdrv\usbd_helper.h>

namespace usbip::filter
{

namespace impl
{

/*
 * 9.4 Standard Device Requests
 * 9.4.1? Get Firmware Status
 * bRequest: GET_FW_STATUS
 * wValue: USB_GET_FIRMWARE_ALLOWED_OR_DISALLOWED_STATE, USB_GET_FIRMWARE_HASH
 * wIndex: Zero
 * wLength: Zero
 */
inline const USB_DEFAULT_PIPE_SETUP_PACKET setup_packet =
{
        .bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        .bRequest = USB_REQUEST_GET_FIRMWARE_STATUS,
        .wValue{.W = USB_GET_FIRMWARE_ALLOWED_OR_DISALLOWED_STATE},
        .wIndex{.W = MAXUSHORT}, // real request should have zero
};

constexpr ULONG const_part = 0xFFFF'0000;
static_assert(sizeof(const_part) == 2*sizeof(USHORT));

} // namespace impl


constexpr auto is_request_function(_In_ int function)
{
        switch (function) {
        case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
        case URB_FUNCTION_SYNC_RESET_PIPE:
        case URB_FUNCTION_SYNC_CLEAR_STALL:
        case URB_FUNCTION_SELECT_INTERFACE:
        case URB_FUNCTION_SELECT_CONFIGURATION:
                return true;
        }

        return false;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto is_request(_In_ const _URB_CONTROL_TRANSFER_EX &r)
{
        return (r.Timeout & impl::const_part) == impl::const_part && 
                r.TransferFlags == ULONG(USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN) &&
                get_setup_packet(r) == impl::setup_packet;
}

inline auto get_function(_Inout_ _URB_CONTROL_TRANSFER_EX &r, _In_ bool clear = false)
{
        static_assert(sizeof(r.Hdr.Function) == sizeof(USHORT));
        static_assert(sizeof(impl::const_part) == sizeof(r.Timeout));

        USHORT function = r.Timeout & USHORT(~0);
        if (clear) {
                r.Timeout = 0;
        }

        NT_ASSERT(is_request_function(function));
        return function;

}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void pack_request(_Out_ _URB_CONTROL_TRANSFER_EX &r, _In_ void *TransferBuffer, _In_ USHORT function);

} // namespace usbip::filter
