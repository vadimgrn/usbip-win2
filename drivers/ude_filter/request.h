/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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

/*
 * URB functions that wrap a vendor or class-specific control transfer in the legacy
 * _URB_CONTROL_VENDOR_OR_CLASS_REQUEST struct. UDECX does not accept these — it expects
 * the URB_FUNCTION_CONTROL_TRANSFER_EX setup-packet form. Translation is required for
 * drivers that build URBs via UsbBuildVendorRequest() (e.g. ftdibus.sys for FTDI D2XX
 * private commands). See issue #167.
 */
constexpr auto is_legacy_vendor_class_function(_In_ int function)
{
        switch (function) {
        case URB_FUNCTION_VENDOR_DEVICE:
        case URB_FUNCTION_VENDOR_INTERFACE:
        case URB_FUNCTION_VENDOR_ENDPOINT:
        case URB_FUNCTION_VENDOR_OTHER:
        case URB_FUNCTION_CLASS_DEVICE:
        case URB_FUNCTION_CLASS_INTERFACE:
        case URB_FUNCTION_CLASS_ENDPOINT:
        case URB_FUNCTION_CLASS_OTHER:
                return true;
        }

        return false;
}

/*
 * Translate a legacy vendor/class URB into URB_FUNCTION_CONTROL_TRANSFER_EX form.
 * @src must satisfy is_legacy_vendor_class_function(src.Hdr.Function).
 * @dst is fully overwritten. TransferBuffer / TransferBufferMDL are passed through unchanged;
 * the caller still owns the buffer.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void translate_legacy_vendor_class(
        _Out_ _URB_CONTROL_TRANSFER_EX &dst,
        _In_ const _URB_CONTROL_VENDOR_OR_CLASS_REQUEST &src);

} // namespace usbip::filter
