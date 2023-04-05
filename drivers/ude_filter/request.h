/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
 * 9.4.14 Set Firmware Status
 * bRequest: SET_FW_STATUS
 * wValue: 0 : Disallow FW update, 1 : Allow FW update, 2 – 0xFF : Reserved
 * wIndex: Zero
 * wLength: Zero
 */
inline const USB_DEFAULT_PIPE_SETUP_PACKET setup_packet =
{
        .bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        .bRequest = USB_REQUEST_SET_FIRMWARE_STATUS,
        .wValue{.W = MAXUSHORT}, // must be zero for genuine request
        .wIndex{.W = MAXUSHORT}
};

constexpr ULONG const_part = 0xFFFF'0000;
static_assert(sizeof(const_part) == 2*sizeof(USHORT));

} // namespace impl


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto is_request(_In_ const _URB_CONTROL_TRANSFER_EX &r)
{
        return (r.Timeout & impl::const_part) == impl::const_part && 
                r.TransferFlags == ULONG(USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT) &&
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
        return function;

}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void pack_request(_Out_ _URB_CONTROL_TRANSFER_EX &r, _In_ void *TransferBuffer, _In_ USHORT function);

} // namespace usbip::filter
