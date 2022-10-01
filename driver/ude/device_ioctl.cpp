/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device_ioctl.h"
#include "trace.h"
#include "device_ioctl.tmh"

#include "context.h"
#include "vhci.h"
#include "device.h"

#include <libdrv\dbgcommon.h>

#include <usbioctl.h>

namespace
{

using namespace usbip;

/*
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto new_wsk_context(_In_ const endpoint_ctx &endp, _Inout_opt_ IRP *irp, _In_ ULONG NumberOfPackets = 0)
{
        if (irp) {
                get_pipe_handle(irp) = handle;
        }

        auto ctx = alloc_wsk_context(NumberOfPackets);
        if (ctx) {
                ctx->vpdo = &vpdo;
                ctx->irp = irp;
        }

        return ctx;
}
*/

using urb_function_t = NTSTATUS (WDFREQUEST, IRP*, URB&, const endpoint_ctx&);

/*
 * Any URBs queued for such an endpoint should normally be unlinked by the driver before clearing the halt condition,
 * as described in sections 5.7.5 and 5.8.5 of the USB 2.0 spec.
 *
 * Thus, a driver must call URB_FUNCTION_ABORT_PIPE before URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL.
 * For that reason abort_pipe(urbr->vpdo, r.PipeHandle) is not called here.
 *
 * Linux server catches control transfer USB_REQ_CLEAR_FEATURE/USB_ENDPOINT_HALT and calls usb_clear_halt which
 * a) Issues USB_REQ_CLEAR_FEATURE/USB_ENDPOINT_HALT # URB_FUNCTION_SYNC_CLEAR_STALL
 * b) Calls usb_reset_endpoint # URB_FUNCTION_SYNC_RESET_PIPE
 *
 * See: <linux>/drivers/usb/usbip/stub_rx.c, is_clear_halt_cmd
 *      <linux>/drivers/usb/core/message.c, usb_clear_halt
 *
 * ###
 * 
 * URB_FUNCTION_SYNC_CLEAR_STALL must issue USB_REQ_CLEAR_FEATURE, USB_ENDPOINT_HALT.
 * URB_FUNCTION_SYNC_RESET_PIPE must call usb_reset_endpoint.
 *
 * Linux server catches control transfer USB_REQ_CLEAR_FEATURE/USB_ENDPOINT_HALT and calls usb_clear_halt.
 * There is no way to distinguish these two operations without modifications on server's side.
 * It can be implemented by passing extra parameter
 * a) wValue=1 to clear halt
 * b) wValue=2 to call usb_reset_endpoint
 *
 * @see <linux>/drivers/usb/usbip/stub_rx.c, is_clear_halt_cmd
 * @see <linux>/drivers/usb/core/message.c, usb_clear_halt, usb_reset_endpoint
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS pipe_request(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        auto &r = urb.UrbPipeRequest;
        NT_ASSERT(r.PipeHandle);

        switch (urb.UrbHeader.Function) {
        case URB_FUNCTION_ABORT_PIPE:
        case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
        case URB_FUNCTION_SYNC_RESET_PIPE:
        case URB_FUNCTION_SYNC_CLEAR_STALL:
        case URB_FUNCTION_CLOSE_STATIC_STREAMS:
                break;
        }
/*
        TraceUrb("irp %04x -> %s: PipeHandle %04x(EndpointAddress %#02x, %!USBD_PIPE_TYPE!, Interval %d)",
                ptr04x(irp),
                urb_function_str(r.Hdr.Function),
                ph4log(r.PipeHandle),
                get_endpoint_address(r.PipeHandle),
                get_endpoint_type(r.PipeHandle),
                get_endpoint_interval(r.PipeHandle));
*/
        TraceUrb("irp %04x -> %s: PipeHandle %04x",
                ptr04x(irp), urb_function_str(r.Hdr.Function), ptr04x(r.PipeHandle));

        UdecxUrbComplete(request, USBD_STATUS_NOT_SUPPORTED);
        return STATUS_PENDING;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS control_get_status_request(
        _In_ WDFREQUEST, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&, _In_ UCHAR recipient)
{
        auto &r = urb.UrbControlGetStatusRequest;

        TraceUrb("irp %04x -> %s: TransferBufferLength %lu (must be 2), %s, Index %hd",
                ptr04x(irp), urb_function_str(r.Hdr.Function), r.TransferBufferLength,
                request_recipient(recipient), r.Index);

        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_status_from_device(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_get_status_request(request, irp, urb, endp, USB_RECIP_DEVICE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_status_from_interface(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_get_status_request(request, irp, urb, endp, USB_RECIP_INTERFACE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_status_from_endpoint(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_get_status_request(request, irp, urb, endp, USB_RECIP_ENDPOINT);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_status_from_other(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_get_status_request(request, irp, urb, endp, USB_RECIP_OTHER);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS control_vendor_class_request(
        _In_ WDFREQUEST, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&, _In_ UCHAR type, _In_ UCHAR recipient)
{
        auto &r = urb.UrbControlVendorClassRequest;

        {
                char buf[USBD_TRANSFER_FLAGS_BUFBZ];
                TraceUrb("irp %04x -> %s: %s, TransferBufferLength %lu, %s, %s, %s(%!#XBYTE!), Value %#hx, Index %#hx",
                        ptr04x(irp), urb_function_str(r.Hdr.Function), usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags),
                        r.TransferBufferLength, request_type(type), request_recipient(recipient), 
                        brequest_str(r.Request), r.Request, r.Value, r.Index);
        }

        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS vendor_device(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_vendor_class_request(request, irp, urb, endp, USB_TYPE_VENDOR, USB_RECIP_DEVICE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS vendor_interface(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_vendor_class_request(request, irp, urb, endp, USB_TYPE_VENDOR, USB_RECIP_INTERFACE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS vendor_endpoint(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_vendor_class_request(request, irp, urb, endp, USB_TYPE_VENDOR, USB_RECIP_ENDPOINT);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS vendor_other(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_vendor_class_request(request, irp, urb, endp, USB_TYPE_VENDOR, USB_RECIP_OTHER);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS class_device(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_vendor_class_request(request, irp, urb, endp, USB_TYPE_CLASS, USB_RECIP_DEVICE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS class_interface(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_vendor_class_request(request, irp, urb, endp, USB_TYPE_CLASS, USB_RECIP_INTERFACE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS class_endpoint(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_vendor_class_request(request, irp, urb, endp, USB_TYPE_CLASS, USB_RECIP_ENDPOINT);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS class_other(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_vendor_class_request(request, irp, urb, endp, USB_TYPE_CLASS, USB_RECIP_OTHER);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS control_descriptor_request(
        _In_ WDFREQUEST, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&,
        _In_ bool dir_in, _In_ UCHAR recipient)
{
        auto &r = urb.UrbControlDescriptorRequest;

        TraceUrb("irp %04x -> %s: TransferBufferLength %lu(%#lx), %s %s, Index %#x, %!usb_descriptor_type!, LanguageId %#04hx",
                ptr04x(irp), urb_function_str(r.Hdr.Function), r.TransferBufferLength, r.TransferBufferLength,
                dir_in ? "IN" : "OUT", request_recipient(recipient),
                r.Index, r.DescriptorType, r.LanguageId);

        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_descriptor_from_device(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_descriptor_request(request, irp, urb, endp, bool(USB_DIR_IN), USB_RECIP_DEVICE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS set_descriptor_to_device(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_descriptor_request(request, irp, urb, endp, bool(USB_DIR_OUT), USB_RECIP_DEVICE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_descriptor_from_interface(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_descriptor_request(request, irp, urb, endp, bool(USB_DIR_IN), USB_RECIP_INTERFACE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS set_descriptor_to_interface(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_descriptor_request(request, irp, urb, endp, bool(USB_DIR_OUT), USB_RECIP_INTERFACE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_descriptor_from_endpoint(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_descriptor_request(request, irp, urb, endp, bool(USB_DIR_IN), USB_RECIP_ENDPOINT);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS set_descriptor_to_endpoint(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_descriptor_request(request, irp, urb, endp, bool(USB_DIR_OUT), USB_RECIP_ENDPOINT);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS control_feature_request(
        _In_ WDFREQUEST, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&,
        _In_ UCHAR bRequest, _In_ UCHAR recipient)
{
        auto &r = urb.UrbControlFeatureRequest;

        TraceUrb("irp %04x -> %s: %s, %s, FeatureSelector %#hx, Index %#hx",
                ptr04x(irp), urb_function_str(r.Hdr.Function), request_recipient(recipient), brequest_str(bRequest),
                r.FeatureSelector, r.Index);

        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS set_feature_to_device(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_feature_request(request, irp, urb, endp, USB_REQUEST_SET_FEATURE, USB_RECIP_DEVICE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS set_feature_to_interface(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_feature_request(request, irp, urb, endp, USB_REQUEST_SET_FEATURE, USB_RECIP_INTERFACE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS set_feature_to_endpoint(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_feature_request(request, irp, urb, endp, USB_REQUEST_SET_FEATURE, USB_RECIP_ENDPOINT);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS set_feature_to_other(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_feature_request(request, irp, urb, endp, USB_REQUEST_SET_FEATURE, USB_RECIP_OTHER);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS clear_feature_to_device(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_feature_request(request, irp, urb, endp, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_DEVICE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS clear_feature_to_interface(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_feature_request(request, irp, urb, endp, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_INTERFACE);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS clear_feature_to_endpoint(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_feature_request(request, irp, urb, endp, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_ENDPOINT);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS clear_feature_to_other(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &endp)
{
        return control_feature_request(request, irp, urb, endp, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_OTHER);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS select_configuration(_In_ WDFREQUEST, _In_ IRP*, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        auto &r = urb.UrbSelectConfiguration;
        auto cd = r.ConfigurationDescriptor; // nullptr if unconfigured

        UCHAR value = cd ? cd->bConfigurationValue : 0;
        TraceUrb("bConfigurationValue %d", value);
        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS select_interface(_In_ WDFREQUEST, _In_ IRP*, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        auto &r = urb.UrbSelectInterface;
        auto &iface = r.Interface;

        TraceUrb("InterfaceNumber %d, AlternateSetting %d", iface.InterfaceNumber, iface.AlternateSetting);
        return STATUS_NOT_IMPLEMENTED;
}

/*
 * Can't be implemented without server's support.
 * In any case the result will be irrelevant due to network latency.
 *
 * See: <linux>//drivers/usb/core/usb.c, usb_get_current_frame_number.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_current_frame_number(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        auto &num = urb.UrbGetCurrentFrameNumber.FrameNumber;
//      num = 0; // vpdo.current_frame_number;

        TraceUrb("irp %04x: FrameNumber %lu", ptr04x(irp), num);

        UdecxUrbComplete(request, USBD_STATUS_NOT_SUPPORTED);
        return STATUS_PENDING;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS control_transfer(
        _In_ WDFREQUEST, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx &)
{
        static_assert(offsetof(_URB_CONTROL_TRANSFER, SetupPacket) == offsetof(_URB_CONTROL_TRANSFER_EX, SetupPacket));
        auto &r = urb.UrbControlTransferEx;

        {
                char buf_flags[USBD_TRANSFER_FLAGS_BUFBZ];
                char buf_setup[USB_SETUP_PKT_STR_BUFBZ];

                TraceUrb("irp %04x -> PipeHandle %04x, %s, TransferBufferLength %lu, Timeout %lu, %s",
                        ptr04x(irp), ptr04x(r.PipeHandle),
                        usbd_transfer_flags(buf_flags, sizeof(buf_flags), r.TransferFlags),
                        r.TransferBufferLength,
                        urb.UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER_EX ? r.Timeout : 0,
                        usb_setup_pkt_str(buf_setup, sizeof(buf_setup), r.SetupPacket));
        }

/*
        auto ctx = new_wsk_context(vpdo, irp, r.PipeHandle);
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (auto err = set_cmd_submit_usbip_header(vpdo, ctx->hdr, r.PipeHandle, r.TransferFlags, r.TransferBufferLength)) {
                free(ctx, false);
                return err;
        }

        if (is_transfer_direction_out(ctx->hdr) != is_transfer_dir_out(urb.UrbControlTransfer)) { // TransferFlags can have wrong direction
                Trace(TRACE_LEVEL_ERROR, "Transfer direction differs in TransferFlags/PipeHandle and SetupPacket");
                free(ctx, false);
                return STATUS_INVALID_PARAMETER;
        }

        static_assert(sizeof(ctx->hdr.u.cmd_submit.setup) == sizeof(r.SetupPacket));
        RtlCopyMemory(ctx->hdr.u.cmd_submit.setup, r.SetupPacket, sizeof(r.SetupPacket));

        return send(ctx, &urb);
*/
        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS bulk_or_interrupt_transfer(_In_ WDFREQUEST, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        auto &r = urb.UrbBulkOrInterruptTransfer;

        {
                auto func = urb.UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL ? ", chained mdl" : " ";
                char buf[USBD_TRANSFER_FLAGS_BUFBZ];

                TraceUrb("irp %04x -> PipeHandle %04x, %s, TransferBufferLength %lu%s",
                        ptr04x(irp), ptr04x(r.PipeHandle), usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags),
                        r.TransferBufferLength, func);
        }

        return STATUS_NOT_IMPLEMENTED;
}

/*
 * USBD_START_ISO_TRANSFER_ASAP is appended because URB_GET_CURRENT_FRAME_NUMBER is not implemented.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS isoch_transfer(_In_ WDFREQUEST, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        auto &r = urb.UrbIsochronousTransfer;

        {
                const char *func = urb.UrbHeader.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL ? ", chained mdl" : ".";
                char buf[USBD_TRANSFER_FLAGS_BUFBZ];
                TraceUrb("irp %04x -> PipeHandle %04x, %s, TransferBufferLength %lu, StartFrame %lu, NumberOfPackets %lu, ErrorCount %lu%s",
                        ptr04x(irp), ptr04x(r.PipeHandle),
                        usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags),
                        r.TransferBufferLength,
                        r.StartFrame,
                        r.NumberOfPackets,
                        r.ErrorCount,
                        func);
        }

        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS function_deprecated(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        TraceUrb("irp %04x: %s not supported", ptr04x(irp), urb_function_str(urb.UrbHeader.Function));

        UdecxUrbComplete(request, USBD_STATUS_NOT_SUPPORTED);
        return STATUS_PENDING;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_configuration(_In_ WDFREQUEST, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        auto &r = urb.UrbControlGetConfigurationRequest;
        TraceUrb("irp %04x -> TransferBufferLength %lu (must be 1)", ptr04x(irp), r.TransferBufferLength);

        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_interface(_In_ WDFREQUEST, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        auto &r = urb.UrbControlGetInterfaceRequest;

        TraceUrb("irp %04x -> TransferBufferLength %lu (must be 1), Interface %hu",
                ptr04x(irp), r.TransferBufferLength, r.Interface);

        return STATUS_NOT_IMPLEMENTED;
}

/*
 * @see https://github.com/libusb/libusb/blob/master/examples/xusb.c, read_ms_winsub_feature_descriptors
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_ms_feature_descriptor(_In_ WDFREQUEST, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        auto &r = urb.UrbOSFeatureDescriptorRequest;

        TraceUrb("irp %04x -> TransferBufferLength %lu, %s, InterfaceNumber %d, MS_PageIndex %d, "
                "MS_FeatureDescriptorIndex %d",
                ptr04x(irp), r.TransferBufferLength, request_recipient(r.Recipient), r.InterfaceNumber, 
                r.MS_PageIndex, r.MS_FeatureDescriptorIndex);

        return STATUS_NOT_IMPLEMENTED;
}

/*
 * See: <kernel>/drivers/usb/core/message.c, usb_set_isoch_delay.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS get_isoch_pipe_transfer_path_delays(
        _In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        auto &r = urb.UrbGetIsochPipeTransferPathDelays;

        TraceUrb("irp %04x -> PipeHandle %04x, MaximumSendPathDelayInMilliSeconds %lu, MaximumCompletionPathDelayInMilliSeconds %lu",
                ptr04x(irp), ptr04x(r.PipeHandle),
                r.MaximumSendPathDelayInMilliSeconds,
                r.MaximumCompletionPathDelayInMilliSeconds);

        UdecxUrbComplete(request, USBD_STATUS_NOT_SUPPORTED);
        return STATUS_PENDING;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS open_static_streams(_In_ WDFREQUEST request, _In_ IRP *irp, _In_ URB &urb, _In_ const endpoint_ctx&)
{
        auto &r = urb.UrbOpenStaticStreams;

        TraceUrb("irp %04x -> PipeHandle %04x, NumberOfStreams %lu, StreamInfoVersion %hu, StreamInfoSize %hu",
                ptr04x(irp), ptr04x(r.PipeHandle), r.NumberOfStreams, r.StreamInfoVersion, r.StreamInfoSize);

        UdecxUrbComplete(request, USBD_STATUS_NOT_SUPPORTED);
        return STATUS_PENDING;
}

urb_function_t* const urb_functions[] =
{
        select_configuration,
        select_interface,
        pipe_request, // URB_FUNCTION_ABORT_PIPE

        function_deprecated, // URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL
        function_deprecated, // URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL

        function_deprecated, // URB_FUNCTION_GET_FRAME_LENGTH
        function_deprecated, // URB_FUNCTION_SET_FRAME_LENGTH
        get_current_frame_number,

        control_transfer,
        bulk_or_interrupt_transfer,
        isoch_transfer,

        get_descriptor_from_device,
        set_descriptor_to_device,

        set_feature_to_device,
        set_feature_to_interface,
        set_feature_to_endpoint,

        clear_feature_to_device,
        clear_feature_to_interface,
        clear_feature_to_endpoint,

        get_status_from_device,
        get_status_from_interface,
        get_status_from_endpoint,

        nullptr, // URB_FUNCTION_RESERVED_0X0016

        vendor_device,
        vendor_interface,
        vendor_endpoint,

        class_device,
        class_interface,
        class_endpoint,

        nullptr, // URB_FUNCTION_RESERVE_0X001D

        pipe_request, // URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL

        class_other,
        vendor_other,

        get_status_from_other,

        set_feature_to_other,
        clear_feature_to_other,

        get_descriptor_from_endpoint,
        set_descriptor_to_endpoint,

        get_configuration,
        get_interface,

        get_descriptor_from_interface,
        set_descriptor_to_interface,

        get_ms_feature_descriptor,

        nullptr, // URB_FUNCTION_RESERVE_0X002B
        nullptr, // URB_FUNCTION_RESERVE_0X002C
        nullptr, // URB_FUNCTION_RESERVE_0X002D
        nullptr, // URB_FUNCTION_RESERVE_0X002E
        nullptr, // URB_FUNCTION_RESERVE_0X002F

        pipe_request, // URB_FUNCTION_SYNC_RESET_PIPE
        pipe_request, // URB_FUNCTION_SYNC_CLEAR_STALL

        control_transfer, // URB_FUNCTION_CONTROL_TRANSFER_EX

        nullptr, // URB_FUNCTION_RESERVE_0X0033
        nullptr, // URB_FUNCTION_RESERVE_0X0034

        open_static_streams,
        pipe_request, // URB_FUNCTION_CLOSE_STATIC_STREAMS

        bulk_or_interrupt_transfer, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL
        isoch_transfer, // URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL

        nullptr, // 0x0039
        nullptr, // 0x003A
        nullptr, // 0x003B
        nullptr, // 0x003C

        get_isoch_pipe_transfer_path_delays // URB_FUNCTION_GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto submit_urb(_In_ WDFREQUEST request, _In_ UDECXUSBENDPOINT endp)
{
        auto irp = WdfRequestWdmGetIrp(request);
        auto &urb = *static_cast<URB*>(URB_FROM_IRP(irp));

        auto func = urb.UrbHeader.Function;
        auto &ctx = *get_endpoint_ctx(endp);

        TraceDbg("%s(%#04x), dev %04x, endp %04x", urb_function_str(func), func, ptr04x(ctx.device), ptr04x(endp));

        if (auto handler = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : nullptr) {
                return handler(request, irp, urb, ctx);
        }

        Trace(TRACE_LEVEL_ERROR, "%s(%#04x) has no handler (reserved?)", urb_function_str(func), func);
        return STATUS_NOT_IMPLEMENTED;
}

} // namespace


/*
 * IRP_MJ_INTERNAL_DEVICE_CONTROL 
 */
_Function_class_(EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI usbip::device::internal_device_control(
        _In_ WDFQUEUE Queue, 
        _In_ WDFREQUEST Request,
        _In_ size_t /*OutputBufferLength*/,
        _In_ size_t /*InputBufferLength*/,
        _In_ ULONG IoControlCode)
{
        if (IoControlCode != IOCTL_INTERNAL_USB_SUBMIT_URB) {
                auto st = STATUS_INVALID_DEVICE_REQUEST;
                Trace(TRACE_LEVEL_ERROR, "%s(%#08lX) %!STATUS!", internal_device_control_name(IoControlCode), 
                        IoControlCode, st);

                WdfRequestComplete(Request, st);
                return;
        }

        auto endp = *get_queue_ctx(Queue);
        auto st = submit_urb(Request, endp);

        if (st != STATUS_PENDING) {
                TraceDbg("%!STATUS!", st);
                UdecxUrbCompleteWithNtStatus(Request, st);
        }
}
