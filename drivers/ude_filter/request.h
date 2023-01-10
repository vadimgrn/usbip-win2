/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\usbd_helper.h>

namespace usbip::filter
{

/*
 * 9.4 Standard Device Requests
 * 9.4.14 Set Firmware Status
 * bRequest: SET_FW_STATUS
 * wValue: 0 : Disallow FW update, 1 : Allow FW update, 2 – 0xFF : Reserved
 * wIndex: Zero
 * wLength: Zero
 */
inline const USB_DEFAULT_PIPE_SETUP_PACKET setup_select =
{
        .bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        .bRequest = USB_REQUEST_SET_FIRMWARE_STATUS,
        .wValue{.W = MAXUSHORT}, // must be zero for genuine request
        .wIndex{.W = MAXUSHORT}
};

namespace impl
{

const ULONG const_part = 0xFFFE'0000;

inline ULONG pack(_In_ UCHAR value, _In_ UCHAR index, _In_ bool cfg_or_intf)
{
        static_assert(sizeof(ULONG) == 4);
        return const_part | ULONG(cfg_or_intf << 16) | ULONG(index << 8) | value;
}

inline void unpack(_In_ ULONG packed, _Out_ USHORT &value, _Out_ USHORT &index, _Out_ bool &cfg_or_intf)
{
        NT_ASSERT((packed & const_part) == const_part);
        value = static_cast<UCHAR>(packed);
        index = static_cast<UCHAR>(packed >> 8);
        cfg_or_intf = (packed >> 16) & 1;
}

} // namespace impl


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto is_request_select(_In_ const _URB_CONTROL_TRANSFER_EX &r)
{
        auto &pkt = get_setup_packet(r);

        return (r.Timeout & impl::const_part) == impl::const_part && 
                r.TransferFlags == ULONG(USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT) &&
                RtlEqualMemory(&pkt, &setup_select, sizeof(pkt));
}

/*
 * UDECXUSBDEVICE never receives USB_REQUEST_SET_CONFIGURATION and USB_REQUEST_SET_INTERFACE 
 * in URB_FUNCTION_CONTROL_TRANSFER because UDE handles them itself. Therefore these requests 
 * are passed as fake/invalid USB_REQUEST_SET_FIRMWARE_STATUS.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void pack_request_select(
        _Out_ _URB_CONTROL_TRANSFER_EX &r, _In_ UCHAR value, _In_ UCHAR index, _In_ bool cfg_or_intf)
{
        r.Hdr.Length = sizeof(r);
        r.Hdr.Function = URB_FUNCTION_CONTROL_TRANSFER_EX;

        r.TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;
        r.Timeout = impl::pack(value, index, cfg_or_intf);

        get_setup_packet(r) = setup_select;
        NT_ASSERT(is_request_select(r));
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void unpack_request_select(_Inout_ _URB_CONTROL_TRANSFER_EX &r)
{
        auto &pkt = get_setup_packet(r);

        bool cfg_or_intf;
        impl::unpack(r.Timeout, pkt.wValue.W, pkt.wIndex.W, cfg_or_intf);
        r.Timeout = 0;

        pkt.bmRequestType.s.Recipient = cfg_or_intf ? USB_RECIP_DEVICE : USB_RECIP_INTERFACE;
        pkt.bRequest = cfg_or_intf ? USB_REQUEST_SET_CONFIGURATION : USB_REQUEST_SET_INTERFACE;
}

} // namespace usbip::filter

