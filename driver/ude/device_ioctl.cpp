/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device_ioctl.h"
#include "trace.h"
#include "device_ioctl.tmh"

#include "context.h"
#include "wsk_context.h"
#include "device.h"
#include "device_queue.h"
#include "proto.h"
#include "network.h"
#include "ioctl.h"
#include "wsk_receive.h"

#include <libdrv\pdu.h>
#include <libdrv\ch9.h>
#include <libdrv\ch11.h>
#include <libdrv\usb_util.h>
#include <libdrv\wsk_cpp.h>
#include <libdrv\dbgcommon.h>
#include <libdrv\usbd_helper.h>

namespace
{

using namespace usbip;

/*
 * wsk_irp->Tail.Overlay.DriverContext[] are zeroed.
 *
 * In general, you must not touch IRP that was put in Cancel-Safe Queue because it can be canceled at any moment.
 * You should remove IRP from the CSQ and then use it. BUT you can access IRP if you shure it is alive.
 *
 * To avoid copying of URB's transfer buffer, it must not be completed until this handler will be called.
 * This means that:
 * 1.EvtIoCanceledOnQueue must not complete IRP if it's called before send_complete because WskSend can still access
 *   IRP transfer buffer.
 * 2.WskReceive must not complete IRP if it's called before send_complete because send_complete modifies request_context.status.
 * 3.EvtIoCanceledOnQueue and WskReceive are mutually exclusive because IRP is dequeued from the CSQ.
 * 4.Thus, send_complete can run concurrently with EvtIoCanceledOnQueue or WskReceive.
 * 
 * @see wsk_receive.cpp, complete 
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS send_complete(
        _In_ DEVICE_OBJECT*, _In_ IRP *wsk_irp, _In_reads_opt_(_Inexpressible_("varies")) void *Context)
{
        wsk_context_ptr ctx(static_cast<wsk_context*>(Context), true);
        auto request = ctx->request;

        request_ctx *req_ctx;
        seqnum_t seqnum;
        request_status old_status;

        if (request) { // NULL for send_cmd_unlink
                req_ctx = get_request_ctx(request);
                seqnum = req_ctx->seqnum;
                old_status = atomic_set_status(*req_ctx, REQ_SEND_COMPLETE);
        } else {
                req_ctx = nullptr;
                seqnum = 0;
                old_status = REQ_NO_HANDLE;
        }

        auto &st = wsk_irp->IoStatus;

        TraceWSK("wsk irp %04x, seqnum %u, %!STATUS!, Information %Iu, %!request_status!", 
                  ptr04x(wsk_irp), seqnum, st.Status, st.Information, old_status);

        if (!request) {
                // nothing to do
        } else if (NT_SUCCESS(st.Status)) { // request has sent
                switch (old_status) {
                case REQ_RECV_COMPLETE: 
                        complete(request);
                        break;
                case REQ_CANCELED:
                        complete(request, STATUS_CANCELLED);
                        break;
                }
        } else if (auto victim = device::dequeue_request(*ctx->dev_ctx, seqnum)) { // ctx->hdr.base.seqnum is in network byte order
                NT_ASSERT(victim == request);
                complete(victim, st.Status);
        } else if (old_status == REQ_CANCELED) {
                complete(request, STATUS_CANCELLED);
        }

        if (st.Status == STATUS_FILE_FORCED_CLOSED) {
                auto dev = get_device(ctx->dev_ctx);
                device::sched_plugout_and_delete(dev);
        }

        return StopCompletion;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto prepare_wsk_buf(_Out_ WSK_BUF &buf, _Inout_ wsk_context &ctx, _Inout_opt_ const URB *transfer_buffer)
{
        NT_ASSERT(!ctx.mdl_buf);

        if (transfer_buffer && is_transfer_dir_out(ctx.hdr)) { // TransferFlags can have wrong direction
                if (auto err = make_transfer_buffer_mdl(ctx.mdl_buf, URB_BUF_LEN, ctx.is_isoc, IoReadAccess, *transfer_buffer)) {
                        Trace(TRACE_LEVEL_ERROR, "make_transfer_buffer_mdl %!STATUS!", err);
                        return err;
                }
        }

        ctx.mdl_hdr.next(ctx.mdl_buf); // always replace tie from previous call

        if (ctx.is_isoc) {
                NT_ASSERT(ctx.mdl_isoc);
                byteswap(ctx.isoc, number_of_packets(ctx));
                auto t = tail(ctx.mdl_hdr); // ctx.mdl_buf can be a chain
                t->Next = ctx.mdl_isoc.get();
        }

        buf.Mdl = ctx.mdl_hdr.get();
        buf.Offset = 0;
        buf.Length = get_total_size(ctx.hdr);

        NT_ASSERT(verify(buf, ctx.is_isoc));
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto send(_In_opt_ UDECXUSBENDPOINT endpoint, _In_ wsk_context_ptr &ctx, _In_ device_ctx &dev,
        _In_ bool log_setup, _Inout_opt_ const URB* transfer_buffer = nullptr)
{
        if (dev.unplugged) {
                return STATUS_DEVICE_REMOVED;
        }

        WSK_BUF buf;

        if (auto err = prepare_wsk_buf(buf, *ctx, transfer_buffer)) {
                return err;
        } else {
                char str[DBG_USBIP_HDR_BUFSZ];
                TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "req %04x -> %Iu%s",
                        ptr04x(ctx->request), buf.Length, dbg_usbip_hdr(str, sizeof(str), &ctx->hdr, log_setup));
        }

        NT_ASSERT(bool(ctx->request) == bool(endpoint));

        if (auto req = ctx->request) {
                auto &req_ctx = *get_request_ctx(req);

                req_ctx.seqnum = ctx->hdr.base.seqnum;
                NT_ASSERT(is_valid_seqnum(req_ctx.seqnum));

                NT_ASSERT(req_ctx.status == REQ_ZEROED);
                req_ctx.endpoint = endpoint;

                if (auto err = WdfRequestForwardToIoQueue(req, dev.queue)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfRequestForwardToIoQueue %!STATUS!", err);
                        return err;
                }
        }

        byteswap_header(ctx->hdr, swap_dir::host2net);

        auto wsk_irp = ctx->wsk_irp; // do not access ctx or wsk_irp after send
        IoSetCompletionRoutine(wsk_irp, send_complete, ctx.release(), true, true, true);

        auto err = send(dev.sock(), &buf, WSK_FLAG_NODELAY, wsk_irp);
        NT_ASSERT(err != STATUS_NOT_SUPPORTED);

        TraceWSK("wsk irp %04x, %Iu bytes, %!STATUS!", ptr04x(wsk_irp), buf.Length, err);
        
        static_assert(NT_SUCCESS(STATUS_PENDING));
        return STATUS_PENDING;
}

using urb_function_t = NTSTATUS (device_ctx&, UDECXUSBENDPOINT, const endpoint_ctx&, WDFREQUEST, URB&);

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS control_transfer(
        _In_ device_ctx &dev, _In_ UDECXUSBENDPOINT endpoint, _In_ const endpoint_ctx &endp,
        _In_ WDFREQUEST request, _In_ URB &urb)
{
        NT_ASSERT(usb_endpoint_type(endp.descriptor) == UsbdPipeTypeControl);

        static_assert(offsetof(_URB_CONTROL_TRANSFER, SetupPacket) == offsetof(_URB_CONTROL_TRANSFER_EX, SetupPacket));
        auto &r = urb.UrbControlTransferEx;

        {
                char buf_flags[USBD_TRANSFER_FLAGS_BUFBZ];
                char buf_setup[USB_SETUP_PKT_STR_BUFBZ];

                TraceUrb("req %04x -> PipeHandle %04x, %s, TransferBufferLength %lu, Timeout %lu, %s",
                        ptr04x(request), ptr04x(r.PipeHandle),
                        usbd_transfer_flags(buf_flags, sizeof(buf_flags), r.TransferFlags),
                        r.TransferBufferLength,
                        urb.UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER_EX ? r.Timeout : 0,
                        usb_setup_pkt_str(buf_setup, sizeof(buf_setup), r.SetupPacket));
        }

        auto buf_len = r.TransferBufferLength;

        if (auto pkt = &get_setup_packet(r)) {
                if (buf_len > pkt->wLength) { // see drivers/usb/core/urb.c, usb_submit_urb
                        buf_len = pkt->wLength; // usb_submit_urb checks for equality
                } else if (buf_len < pkt->wLength) {
                        Trace(TRACE_LEVEL_ERROR, "TransferBufferLength(%lu) < wLength(%d)", buf_len, pkt->wLength);
                        return STATUS_INVALID_PARAMETER;
                }
        }

        wsk_context_ptr ctx(&dev, request);
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }
        
        setup_dir dir_out = is_transfer_dir_out(urb.UrbControlTransfer); // default control pipe is bidirectional

        if (auto err = set_cmd_submit_usbip_header(ctx->hdr, dev, endp.descriptor, r.TransferFlags, buf_len, dir_out)) {
                return err;
        }

        static_assert(sizeof(ctx->hdr.u.cmd_submit.setup) == sizeof(r.SetupPacket));
        RtlCopyMemory(ctx->hdr.u.cmd_submit.setup, r.SetupPacket, sizeof(r.SetupPacket));

        return send(endpoint, ctx, dev, true, &urb);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS bulk_or_interrupt_transfer(
        _In_ device_ctx &dev, _In_ UDECXUSBENDPOINT endpoint, _In_ const endpoint_ctx &endp,
        _In_ WDFREQUEST request, _In_ URB &urb)
{
        NT_ASSERT(usb_endpoint_type(endp.descriptor) == UsbdPipeTypeBulk || 
                  usb_endpoint_type(endp.descriptor) == UsbdPipeTypeInterrupt);

        auto &r = urb.UrbBulkOrInterruptTransfer;

        {
                auto func = urb.UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL ? ", MDL" : " ";
                char buf[USBD_TRANSFER_FLAGS_BUFBZ];

                TraceUrb("req %04x -> PipeHandle %04x, %s, TransferBufferLength %lu%s",
                        ptr04x(request), ptr04x(r.PipeHandle), usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags),
                        r.TransferBufferLength, func);
        }

        wsk_context_ptr ctx(&dev, request);
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (auto err = set_cmd_submit_usbip_header(ctx->hdr, dev, endp.descriptor, r.TransferFlags, r.TransferBufferLength)) {
                return err;
        }

        return send(endpoint, ctx, dev, false, &urb);
}

/*
 * USBD_ISO_PACKET_DESCRIPTOR.Length is not used (zero) for USB_DIR_OUT transfer.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
auto repack(_In_ usbip_iso_packet_descriptor *d, _In_ const _URB_ISOCH_TRANSFER &r)
{
        ULONG length = 0;

        for (ULONG i = 0; i < r.NumberOfPackets; ++d) {

                auto offset = r.IsoPacket[i].Offset;
                auto next_offset = ++i < r.NumberOfPackets ? r.IsoPacket[i].Offset : r.TransferBufferLength;

                if (next_offset >= offset && next_offset <= r.TransferBufferLength) {
                        d->offset = offset;
                        d->length = next_offset - offset;
                        d->actual_length = 0;
                        d->status = 0;
                        length += d->length;
                } else {
                        Trace(TRACE_LEVEL_ERROR, "[%lu] next_offset(%lu) >= offset(%lu) && next_offset <= r.TransferBufferLength(%lu)",
                                i, next_offset, offset, r.TransferBufferLength);
                        return STATUS_INVALID_PARAMETER;
                }
        }

        NT_ASSERT(length == r.TransferBufferLength);
        return STATUS_SUCCESS;
}

/*
 * USBD_START_ISO_TRANSFER_ASAP is appended because URB_GET_CURRENT_FRAME_NUMBER is not implemented.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(urb_function_t)
NTSTATUS isoch_transfer(
        _In_ device_ctx &dev, _In_ UDECXUSBENDPOINT endpoint, _In_ const endpoint_ctx &endp,
        _In_ WDFREQUEST request, _In_ URB &urb)
{
        NT_ASSERT(usb_endpoint_type(endp.descriptor) == UsbdPipeTypeIsochronous);
        auto &r = urb.UrbIsochronousTransfer;

        {
                const char *func = urb.UrbHeader.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL ? ", MDL" : " ";
                char buf[USBD_TRANSFER_FLAGS_BUFBZ];
                TraceUrb("req %04x -> PipeHandle %04x, %s, TransferBufferLength %lu, StartFrame %lu, NumberOfPackets %lu, ErrorCount %lu%s",
                        ptr04x(request), ptr04x(r.PipeHandle),
                        usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags),
                        r.TransferBufferLength,
                        r.StartFrame,
                        r.NumberOfPackets,
                        r.ErrorCount,
                        func);
        }

        if (r.NumberOfPackets > USBIP_MAX_ISO_PACKETS) {
                Trace(TRACE_LEVEL_ERROR, "NumberOfPackets(%lu) > USBIP_MAX_ISO_PACKETS(%d)", 
                                          r.NumberOfPackets, USBIP_MAX_ISO_PACKETS);
                return STATUS_INVALID_PARAMETER;
        }

        wsk_context_ptr ctx(&dev, request, r.NumberOfPackets);
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (auto err = set_cmd_submit_usbip_header(ctx->hdr, dev, endp.descriptor, 
                               r.TransferFlags | USBD_START_ISO_TRANSFER_ASAP, r.TransferBufferLength)) {
                return err;
        }

        if (auto err = repack(ctx->isoc, r)) {
                return err;
        }

        if (auto cmd = &ctx->hdr.u.cmd_submit) {
                cmd->start_frame = r.StartFrame;
                cmd->number_of_packets = r.NumberOfPackets;
        }

        return send(endpoint, ctx, dev, false, &urb);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto usb_submit_urb(
        _In_ device_ctx &dev, _In_ UDECXUSBENDPOINT endpoint, _In_ endpoint_ctx &endp, _In_ WDFREQUEST request)
{
        auto &urb = get_urb(request);
        urb_function_t *handler{};

        switch (auto func = urb.UrbHeader.Function) {
        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL:
                handler = bulk_or_interrupt_transfer;
                break;
        case URB_FUNCTION_ISOCH_TRANSFER:
        case URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL:
                handler = isoch_transfer;
                break;
        case URB_FUNCTION_CONTROL_TRANSFER_EX:
        case URB_FUNCTION_CONTROL_TRANSFER:
                handler = control_transfer;
                break;
        default:
                Trace(TRACE_LEVEL_ERROR, "%s(%#04x), dev %04x, endp %04x", urb_function_str(func), func, 
                                          ptr04x(endp.device), ptr04x(endpoint));

                return STATUS_NOT_SUPPORTED;
        }

        return handler(dev, endpoint, endp, request, urb);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto verify_select(_In_ WDFREQUEST request, _In_ ULONG expected_ioctl)
{
        auto irp = WdfRequestWdmGetIrp(request);
        auto stack = IoGetCurrentIrpStackLocation(irp);
        auto ioctl = DeviceIoControlCode(stack);

        if (stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL && ioctl == expected_ioctl) {
                return STATUS_SUCCESS;
        }
        
        Trace(TRACE_LEVEL_ERROR, "IoControlCode %s(%#x) expected, got %s(%#x)", 
                        internal_device_control_name(expected_ioctl), expected_ioctl,
                        internal_device_control_name(ioctl), ioctl);

        return STATUS_INVALID_DEVICE_REQUEST;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto send_ep0_out(_In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request, 
        _In_ UCHAR bmRequestType, _In_ UCHAR bRequest, _In_ USHORT wValue, _In_ USHORT wIndex)
{
        auto &dev = *get_device_ctx(device);

        wsk_context_ptr ctx(&dev, request);
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto &ep0 = *get_endpoint_ctx(dev.ep0);
        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        if (auto err = set_cmd_submit_usbip_header(ctx->hdr, dev, ep0.descriptor, TransferFlags, 0, setup_dir::out())) {
                return err;
        }

        auto &pkt = get_submit_setup(ctx->hdr);
        pkt.bmRequestType.B = bmRequestType;
        pkt.bRequest = bRequest;
        pkt.wValue.W = wValue;
        pkt.wIndex.W = wIndex;
        NT_ASSERT(!pkt.wLength);

        NT_ASSERT(is_transfer_dir_out(pkt));
        return ::send(dev.ep0, ctx, dev, true);
}

/*
 * WdfRequestGetIoQueue(request) returns queue that does not belong to the device (not its EP0 or others).
 * get_endpoint(WdfRequestGetIoQueue(request)) causes BSOD.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto do_select(_In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request, _In_ ULONG params)
{
        bool iface = params >> 16; 

        UCHAR bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | UCHAR(iface ? USB_RECIP_INTERFACE : USB_RECIP_DEVICE);
        UCHAR bRequest = iface ? USB_REQUEST_SET_INTERFACE : USB_REQUEST_SET_CONFIGURATION;

        USHORT wValue;
        USHORT wIndex;

        if (iface) {
                wValue = UCHAR(params >> 8); // Alternative Setting
                wIndex = UCHAR(params); // Interface
        } else {
                wValue = UCHAR(params); // Configuration Value
                wIndex = 0;
        }

        return send_ep0_out(device, request, bmRequestType, bRequest, wValue, wIndex);
}

} // namespace


 /*
  * There is a race condition between IRP cancelation and RET_SUBMIT.
  * Sequence of events:
  * 1.IRP is waiting for RET_SUBMIT in CSQ.
  * 2.An upper driver cancels IRP.
  * 3.IRP is removed from CSQ, IO_CSQ_COMPLETE_CANCELED_IRP callback is called.
  * 4.The callback inserts IRP into a list of unlinked IRPs (this step is imaginary).
  *
  * RET_SUBMIT can be received
  * a)Before #3 - normal case, IRP will be dequeued from CSQ.
  * b)Before #4 - IRP will not be found in CSQ and the list.
  * c)After #4 - IRP will be found in the list.
  *
  * Case b) is unavoidable because CSQ library calls IO_CSQ_COMPLETE_CANCELED_IRP after releasing a lock.
  * For that reason the cancellation logic is simplified and list of unlinked IRPs is not used.
  * RET_SUBMIT and RET_INLINK must be ignored if IRP is not found (IRP was cancelled and completed).
  */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::device::send_cmd_unlink(_In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request)
{
        auto &dev = *get_device_ctx(device);
        auto &req = *get_request_ctx(request);

        TraceDbg("dev %04x, seqnum %u", ptr04x(device), req.seqnum);

        if (dev.unplugged) {
                TraceDbg("Unplugged");
        } else if (auto ctx = wsk_context_ptr(&dev, WDFREQUEST(WDF_NO_HANDLE))) {
                set_cmd_unlink_usbip_header(ctx->hdr, dev, req.seqnum);
                ::send(WDF_NO_HANDLE, ctx, dev, false); // ignore error
        } else {
                Trace(TRACE_LEVEL_ERROR, "dev %04x, seqnum %u, wsk_context_ptr error", ptr04x(device), req.seqnum);
        }

        if (auto old_status = atomic_set_status(req, REQ_CANCELED); old_status == REQ_SEND_COMPLETE) {
                complete(request, STATUS_CANCELLED);
        } else {
                NT_ASSERT(old_status != REQ_RECV_COMPLETE);
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::set_configuration(
        _In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request, _In_ ULONG ioctl, _In_ UCHAR ConfigurationValue)
{
        TraceDbg("dev %04x, ConfigurationValue %d", ptr04x(device), ConfigurationValue);

        if (auto err = verify_select(request, ioctl)) {
                return err;
        }

        if (auto dev = get_device_ctx(device)) {
                RtlFillMemory(dev->AlternateSetting, sizeof(dev->AlternateSetting), -1);
        }

        return do_select(device, request, ConfigurationValue);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::set_interface(
        _In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request, _In_ UCHAR InterfaceNumber, _In_ UCHAR AlternateSetting)
{
        TraceDbg("dev %04x, InterfaceNumber %d, AlternateSetting %d", ptr04x(device), InterfaceNumber, AlternateSetting);

        if (auto err = verify_select(request, IOCTL_INTERNAL_USBEX_CFG_CHANGE)) {
                return err;
        }

        auto &dev = *get_device_ctx(device);

        if (InterfaceNumber >= ARRAYSIZE(dev.AlternateSetting)) {
                Trace(TRACE_LEVEL_ERROR, "InterfaceNumber %d >= device_ctx.AlternateSetting[%d]", 
                                          InterfaceNumber, ARRAYSIZE(dev.AlternateSetting));
                return STATUS_INVALID_PARAMETER;
        }

        if (dev.AlternateSetting[InterfaceNumber] == AlternateSetting) {
                return STATUS_SUCCESS;
        }

        if (auto req = get_request_ctx(request)) {
                req->pre_complete = [] (auto &req, auto &dev, auto status)
                {
                        if (NT_SUCCESS(status)) {
                                auto &r = req.pre_complete_args.set_intf;
                                dev.AlternateSetting[r.InterfaceNumber] = r.AlternateSetting;
                        }
                };

                if (auto r = &req->pre_complete_args.set_intf) {
                        r->InterfaceNumber = InterfaceNumber;
                        r->AlternateSetting = AlternateSetting;
                }
        }

        auto params = (1UL << 16) | (AlternateSetting << 8) | InterfaceNumber;
        return do_select(device, request, params);
}

/*
 * @see <linux>/drivers/usb/core/message.c, usb_clear_halt
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::clear_endpoint_stall(_In_ UDECXUSBENDPOINT endpoint, _In_ WDFREQUEST request)
{
        auto &endp = *get_endpoint_ctx(endpoint);
        auto addr = endp.descriptor.bEndpointAddress;
        
        return send_ep0_out(endp.device, request, 
                USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT,
                USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_STALL, addr);
}

/*
 * Call usb_reset_device on Linux side - warn interface drivers and perform a USB port reset.
 * @see <linux>/drivers/usb/usbip/stub_rx.c, is_reset_device_cmd, tweak_reset_device_cmd
 * @see <linux>/drivers/usb/usbip/vhci_hcd.c, vhci_hub_control
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::reset_port(_In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request)
{
        auto &dev = *get_device_ctx(device);

        NT_ASSERT(dev.port >= 1);
        auto port = static_cast<USHORT>(dev.port); // meaningless for a server which ignores it

        static_assert(USB_RT_PORT == (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER));

        return send_ep0_out(device, request, 
                USB_RT_PORT, USB_REQUEST_SET_FEATURE, USB_PORT_FEAT_RESET, port);
}

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

        auto endpoint = get_endpoint(Queue);
        auto &endp = *get_endpoint_ctx(endpoint);
        auto &dev = *get_device_ctx(endp.device);

        if (dev.unplugged) {
                UdecxUrbComplete(Request, USBD_STATUS_DEVICE_GONE); 
                return;
        }

        auto st = usb_submit_urb(dev, endpoint, endp, Request);

        if (st != STATUS_PENDING) {
                TraceDbg("%!STATUS!", st);
                UdecxUrbCompleteWithNtStatus(Request, st);
        }
}
