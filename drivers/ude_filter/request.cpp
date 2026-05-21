/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "request.h"

/*
 * UDECXUSBDEVICE never receives USB_REQUEST_SET_CONFIGURATION and USB_REQUEST_SET_INTERFACE
 * in URB_FUNCTION_CONTROL_TRANSFER because UDE handles them itself. Therefore these requests
 * are passed as USB_REQUEST_GET_FIRMWARE_STATUS.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::filter::pack_request(
        _Out_ _URB_CONTROL_TRANSFER_EX &r, _In_ void *TransferBuffer, _In_ USHORT function)
{
        r.Hdr.Length = sizeof(r);
        r.Hdr.Function = URB_FUNCTION_CONTROL_TRANSFER_EX;

        r.TransferBuffer = TransferBuffer;
        NT_ASSERT(!r.TransferBufferLength);

        r.TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;
        r.Timeout = impl::const_part | function;

        get_setup_packet(r) = impl::setup_packet;

        NT_ASSERT(is_request(r));
        NT_ASSERT(get_function(r) == function);
}

namespace
{

/*
 * Derive the (type, recipient) pair for the setup packet's bmRequestType byte from
 * a legacy vendor/class URB function code.
 */
constexpr auto setup_type_recipient(_In_ USHORT function) -> UCHAR
{
        switch (function) {
        case URB_FUNCTION_VENDOR_DEVICE:    return UCHAR(USB_TYPE_VENDOR | USB_RECIP_DEVICE);
        case URB_FUNCTION_VENDOR_INTERFACE: return UCHAR(USB_TYPE_VENDOR | USB_RECIP_INTERFACE);
        case URB_FUNCTION_VENDOR_ENDPOINT:  return UCHAR(USB_TYPE_VENDOR | USB_RECIP_ENDPOINT);
        case URB_FUNCTION_VENDOR_OTHER:     return UCHAR(USB_TYPE_VENDOR | USB_RECIP_OTHER);
        case URB_FUNCTION_CLASS_DEVICE:     return UCHAR(USB_TYPE_CLASS  | USB_RECIP_DEVICE);
        case URB_FUNCTION_CLASS_INTERFACE:  return UCHAR(USB_TYPE_CLASS  | USB_RECIP_INTERFACE);
        case URB_FUNCTION_CLASS_ENDPOINT:   return UCHAR(USB_TYPE_CLASS  | USB_RECIP_ENDPOINT);
        case URB_FUNCTION_CLASS_OTHER:      return UCHAR(USB_TYPE_CLASS  | USB_RECIP_OTHER);
        }
        NT_ASSERTMSG("unexpected URB function", false);
        return 0;
}

} // namespace

/*
 * Build the URB_FUNCTION_CONTROL_TRANSFER_EX equivalent of a legacy vendor/class URB.
 * Mirrors what usbhub3.sys does internally for real USB devices. The transfer goes
 * out on the default control pipe; the buffer and MDL ownership stay with the caller.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::filter::translate_legacy_vendor_class(
        _Out_ _URB_CONTROL_TRANSFER_EX &dst,
        _In_ const _URB_CONTROL_VENDOR_OR_CLASS_REQUEST &src)
{
        const auto function = src.Hdr.Function;
        NT_ASSERT(is_legacy_vendor_class_function(function));

        RtlZeroMemory(&dst, sizeof(dst));

        dst.Hdr.Length   = sizeof(dst);
        dst.Hdr.Function = URB_FUNCTION_CONTROL_TRANSFER_EX;

        dst.PipeHandle           = nullptr; // default control pipe
        dst.TransferFlags        = src.TransferFlags | USBD_DEFAULT_PIPE_TRANSFER;
        dst.TransferBufferLength = src.TransferBufferLength;
        dst.TransferBuffer       = src.TransferBuffer;
        dst.TransferBufferMDL    = src.TransferBufferMDL;
        dst.Timeout              = 0;

        const bool dir_in = src.TransferFlags & USBD_TRANSFER_DIRECTION_IN;

        // Note: _URB_CONTROL_VENDOR_OR_CLASS_REQUEST::RequestTypeReservedBits is documented
        // by Microsoft as "Reserved. Do not use." We intentionally ignore it — the direction,
        // type, and recipient are fully determined by the URB function code and TransferFlags.
        auto &setup = get_setup_packet(dst);
        setup.bmRequestType = UCHAR((dir_in ? USB_DIR_IN : USB_DIR_OUT)
                                  | setup_type_recipient(function));
        setup.bRequest      = src.Request;
        setup.wValue.W      = src.Value;
        setup.wIndex.W      = src.Index;
        setup.wLength       = USHORT(src.TransferBufferLength);
}
