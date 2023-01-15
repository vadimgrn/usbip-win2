/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

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
inline const USB_DEFAULT_PIPE_SETUP_PACKET setup_select =
{
        .bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        .bRequest = USB_REQUEST_SET_FIRMWARE_STATUS,
        .wValue{.W = MAXUSHORT}, // must be zero for genuine request
        .wIndex{.W = MAXUSHORT}
};

const auto const_part = MAXULONG << 1;

} // namespace impl


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto is_request_select(_In_ const _URB_CONTROL_TRANSFER_EX &r)
{
        auto &pkt = get_setup_packet(r);

        return (r.Timeout & impl::const_part) == impl::const_part && 
                r.TransferFlags == ULONG(USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT) &&
                pkt == impl::setup_select;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void pack_request_select(_Out_ _URB_CONTROL_TRANSFER_EX &r, _In_ void *TransferBuffer, _In_ bool cfg_or_intf);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void unpack_request_select(_Inout_ _URB_CONTROL_TRANSFER_EX &r)
{
        bool cfg_or_intf = r.Timeout & ~impl::const_part;
        r.Timeout = 0;

        if (auto &pkt = get_setup_packet(r); cfg_or_intf) {
                auto urb = reinterpret_cast<_URB_SELECT_CONFIGURATION*>(r.TransferBuffer);
                auto cd = urb->ConfigurationDescriptor; // null if unconfigured

                pkt.bmRequestType.s.Recipient = USB_RECIP_DEVICE;
                pkt.bRequest = USB_REQUEST_SET_CONFIGURATION;
                pkt.wValue.W = cd ? cd->bConfigurationValue : 0; // FIXME: can't pass -1 if unconfigured
                pkt.wIndex.W = 0;
        } else {
                auto urb = reinterpret_cast<_URB_SELECT_INTERFACE*>(r.TransferBuffer);
                auto &intf = urb->Interface;

                pkt.bmRequestType.s.Recipient = USB_RECIP_INTERFACE;
                pkt.bRequest = USB_REQUEST_SET_INTERFACE;
                pkt.wValue.W = intf.AlternateSetting;
                pkt.wIndex.W = intf.InterfaceNumber;
        }

        NT_ASSERT(!r.TransferBufferLength);
}

} // namespace usbip::filter
