#include "internal_ioctl.h"
#include "dev.h"
#include "trace.h"
#include "internal_ioctl.tmh"

#include "dbgcommon.h"
#include "irp.h"
#include "csq.h"
#include "proto.h"
#include "ch11.h"
#include "usb_util.h"
#include "urbtransfer.h"
#include "usbd_helper.h"
#include "usbip_network.h"
#include "pdu.h"
#include "send_context.h"
#include "vhub.h"

namespace
{

auto complete_internal_ioctl(IRP *irp, NTSTATUS status)
{
        TraceMsg("irp %04x %!STATUS!", ptr4log(irp), status);
//      NT_ASSERT(!irp->IoStatus.Information); // can fail
        return CompleteRequest(irp, status);
}

/*
 * wsk_irp->Tail.Overlay.DriverContext[] are zeroed.
 * 
 * In general, you must not touch IRP that was put in Cancel-Safe Queue because it can be canceled at any moment.
 * You should remove IRP from the CSQ and then use it. BUT you can access IRP if you shure it is alive.
 *
 * To avoid copying of URB's transfer buffer, it must not be completed until this handler will be called.
 * This means that:
 * 1.CompleteCanceledIrp must not complete IRP if it was called before send_complete because WskSend can still access
 *   IRP transfer buffer.
 * 2.WskReceiveEvent must not complete IRP if it was called before send_complete because it modifies *get_status(irp).
 * 3.CompleteCanceledIrp and WskReceiveEvent are mutually exclusive because IRP was dequeued from the CSQ.
 * 4.Thus, send_complete can run concurrently with CompleteCanceledIrp or WskReceiveEvent.
 */
NTSTATUS send_complete(_In_ DEVICE_OBJECT*, _In_ IRP *wsk_irp, _In_reads_opt_(_Inexpressible_("varies")) void *Context)
{
        auto ctx = static_cast<send_context*>(Context);
        NT_ASSERT(ctx->wsk_irp == wsk_irp);

        auto &vpdo = *ctx->vpdo;
        auto irp = ctx->irp;
        auto seqnum = get_seqnum(irp); // ctx->hdr.base.seqnum is in network byte order

        auto &st = wsk_irp->IoStatus;
        auto old_status = InterlockedCompareExchange(get_status(irp), ST_SEND_COMPLETE, ST_NONE);

        TraceWSK("irql %!irql!, wsk irp %04x, %!STATUS!, Information %Iu, %!irp_status_t!", 
                  KeGetCurrentIrql(), ptr4log(wsk_irp), st.Status, st.Information, old_status);

        if (NT_SUCCESS(st.Status)) { // request has sent
                switch (old_status) {
                case ST_RECV_COMPLETE:
                        TraceDbg("Complete irp %04x, %!STATUS!, Information %#Ix",
                                  ptr4log(irp), irp->IoStatus.Status, irp->IoStatus.Information);
                        IoCompleteRequest(irp, IO_NO_INCREMENT);
                        break;
                case ST_IRP_CANCELED:
                        complete_canceled_irp(irp);
                        break;
                }
        } else if (auto victim = dequeue_irp(vpdo, seqnum)) {
                NT_ASSERT(victim == irp);
                complete_internal_ioctl(victim, STATUS_UNSUCCESSFUL);
        } else if (old_status == ST_IRP_CANCELED) {
              complete_canceled_irp(irp);
        }

        if (st.Status == STATUS_FILE_FORCED_CLOSED) {
                vhub_unplug_vpdo(&vpdo);
        }

        free(ctx);
        return StopCompletion;
}

auto async_send(_Inout_opt_ const URB &transfer_buffer, _In_ send_context *ctx)
{
        if (is_transfer_direction_in(&ctx->hdr)) { // TransferFlags can have wrong direction
                NT_ASSERT(!ctx->mdl_buf);
        } else if (auto err = usbip::make_transfer_buffer_mdl(ctx->mdl_buf, transfer_buffer)) {
                Trace(TRACE_LEVEL_ERROR, "make_buffer_mdl %!STATUS!", err);
                free(ctx);
                return err;
        }

        ctx->mdl_hdr.next(ctx->mdl_buf);

        if (ctx->is_isoc) {
                NT_ASSERT(ctx->mdl_isoc);
                auto &tail = ctx->mdl_buf ? ctx->mdl_buf : ctx->mdl_hdr;
                tail.next(ctx->mdl_isoc);
                byteswap(ctx->isoc, number_of_packets(*ctx));
        }

        WSK_BUF buf{ ctx->mdl_hdr.get(), 0, get_total_size(ctx->hdr) };

        NT_ASSERT(buf.Length >= ctx->mdl_hdr.size());
        NT_ASSERT(buf.Length <= size(ctx->mdl_hdr)); // MDL for TransferBuffer can be larger than TransferBufferLength

        {
                char str[DBG_USBIP_HDR_BUFSZ];
                TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "irp %04x -> %Iu%s", 
                            ptr4log(ctx->irp), buf.Length, dbg_usbip_hdr(str, sizeof(str), &ctx->hdr, false));
        }

        byteswap_header(ctx->hdr, swap_dir::host2net);

        IoSetCompletionRoutine(ctx->wsk_irp, send_complete, ctx, true, true, true);
        enqueue_irp(*ctx->vpdo, ctx->irp);

        auto err = send(ctx->vpdo->sock, &buf, WSK_FLAG_NODELAY, ctx->wsk_irp);
        NT_ASSERT(err != STATUS_NOT_SUPPORTED);

        TraceWSK("wsk irp %04x, %!STATUS!", ptr4log(ctx->wsk_irp), err);
        return STATUS_PENDING;
}

/*
 * I/O Manager can issue IoCancelIrp only when a driver returned STATUS_PENDING from its dispatch routine.
 * This means during the execution of dispatch routine IRP can't be canceled.
 */
auto send(_Inout_ vpdo_dev_t &vpdo, _Inout_ IRP *irp, _Inout_ usbip_header &hdr, 
        _Inout_opt_ const URB *transfer_buffer = nullptr, _In_ USBD_PIPE_HANDLE hpipe = USBD_PIPE_HANDLE())
{
        auto seqnum = hdr.base.seqnum;
        set_context(irp, seqnum, ST_SEND_COMPLETE, hpipe);

        enqueue_irp(vpdo, irp);

        if (auto err = usbip::send_cmd(vpdo.sock, hdr, transfer_buffer)) {
                NT_VERIFY(dequeue_irp(vpdo, seqnum)); // hdr.base.seqnum is in network byte order
                if (err == STATUS_FILE_FORCED_CLOSED) {
                        vhub_unplug_vpdo(&vpdo);
                }
                return err;
        }

        return STATUS_PENDING;
}

/*
 * PAGED_CODE() fails.
 * USBD_ISO_PACKET_DESCRIPTOR.Length is not used (zero) for USB_DIR_OUT transfer.
 */
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

NTSTATUS abort_pipe(vpdo_dev_t &vpdo, USBD_PIPE_HANDLE PipeHandle)
{
	TraceUrb("PipeHandle %#Ix", ph4log(PipeHandle));

	if (!PipeHandle) {
		return STATUS_INVALID_PARAMETER;
	}

	auto ctx = make_peek_context(PipeHandle);

	while (auto irp = IoCsqRemoveNextIrp(&vpdo.irps_csq, &ctx)) {
                send_cmd_unlink(vpdo, irp);
	}

	return STATUS_SUCCESS;
}

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
 */
PAGEABLE NTSTATUS sync_reset_pipe_and_clear_stall(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();

        auto &r = urb.UrbPipeRequest;
        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT;
        pkt->bRequest = USB_REQUEST_CLEAR_FEATURE;
        pkt->wValue.W = USB_FEATURE_ENDPOINT_STALL; // USB_ENDPOINT_HALT
        pkt->wIndex.W = get_endpoint_address(r.PipeHandle);

        return send(vpdo, irp, hdr);
}

/*
 * URB_FUNCTION_SYNC_CLEAR_STALL must issue USB_REQ_CLEAR_FEATURE, USB_ENDPOINT_HALT.
 * URB_FUNCTION_SYNC_RESET_PIPE must call usb_reset_endpoint.
 * 
 * Linux server catches control transfer USB_REQ_CLEAR_FEATURE/USB_ENDPOINT_HALT and calls usb_clear_halt.
 * There is no way to distinguish these two operations without modifications on server's side.
 * It can be implemented by passing extra parameter
 * a) wValue=1 to clear halt 
 * b) wValue=2 to call usb_reset_endpoint
 * 
 * See: <linux>/drivers/usb/usbip/stub_rx.c, is_clear_halt_cmd
 * <linux>/drivers/usb/core/message.c, usb_clear_halt, usb_reset_endpoint
 */
NTSTATUS pipe_request(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        auto &r = urb.UrbPipeRequest;
        auto st = STATUS_NOT_SUPPORTED;

        switch (urb.UrbHeader.Function) {
        case URB_FUNCTION_ABORT_PIPE:
                st = abort_pipe(vpdo, r.PipeHandle);
                break;
        case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
                st = sync_reset_pipe_and_clear_stall(vpdo, irp, urb);
                break;
        case URB_FUNCTION_SYNC_RESET_PIPE:
        case URB_FUNCTION_SYNC_CLEAR_STALL:
        case URB_FUNCTION_CLOSE_STATIC_STREAMS:
                urb.UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;
                break;
        }

        TraceUrb("irp %04x -> %s: PipeHandle %#Ix(EndpointAddress %#02x, %!USBD_PIPE_TYPE!, Interval %d) -> %!STATUS!",
                ptr4log(irp),
                urb_function_str(r.Hdr.Function), 
                ph4log(r.PipeHandle), 
                get_endpoint_address(r.PipeHandle), 
                get_endpoint_type(r.PipeHandle),
                get_endpoint_interval(r.PipeHandle),
                st);

        return st;
}

PAGEABLE NTSTATUS control_get_status_request(vpdo_dev_t &vpdo, IRP *irp, URB &urb, UCHAR recipient)
{
        PAGED_CODE();

        auto &r = urb.UrbControlGetStatusRequest;

        TraceUrb("irp %04x -> %s: TransferBufferLength %lu (must be 2), Index %hd", 
                ptr4log(irp), urb_function_str(r.Hdr.Function), r.TransferBufferLength, r.Index);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, r.TransferBufferLength)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | recipient;
        pkt->bRequest = USB_REQUEST_GET_STATUS;
        pkt->wIndex.W = r.Index;
        pkt->wLength = (USHORT)r.TransferBufferLength; // must be 2

        return send(vpdo, irp, hdr, &urb);
}

PAGEABLE NTSTATUS get_status_from_device(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_get_status_request(vpdo, irp, urb, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS get_status_from_interface(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_get_status_request(vpdo, irp, urb, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS get_status_from_endpoint(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_get_status_request(vpdo, irp, urb, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS get_status_from_other(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_get_status_request(vpdo, irp, urb, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS control_vendor_class_request(vpdo_dev_t &vpdo, IRP *irp, URB &urb, UCHAR type, UCHAR recipient)
{
        PAGED_CODE();

        auto &r = urb.UrbControlVendorClassRequest;

        {
                char buf[USBD_TRANSFER_FLAGS_BUFBZ];
                TraceUrb("irp %04x -> %s: %s, TransferBufferLength %lu, %s(%!#XBYTE!), Value %#hx, Index %#hx",
                        ptr4log(irp), urb_function_str(r.Hdr.Function), usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags), 
                        r.TransferBufferLength, brequest_str(r.Request), r.Request, r.Value, r.Index);
        }

        usbip_header hdr{};
        auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, r.TransferFlags | USBD_DEFAULT_PIPE_TRANSFER, 
                                               r.TransferBufferLength);

        if (err) {
                return err;
        }

        bool dir_out = is_transfer_direction_out(&hdr); // TransferFlags can have wrong direction

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = UCHAR((dir_out ? USB_DIR_OUT : USB_DIR_IN) | type | recipient);
        pkt->bRequest = r.Request;
        pkt->wValue.W = r.Value;
        pkt->wIndex.W = r.Index;
        pkt->wLength = (USHORT)r.TransferBufferLength;

        return send(vpdo, irp, hdr, &urb);
}

PAGEABLE NTSTATUS vendor_device(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_VENDOR, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS vendor_interface(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_VENDOR, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS vendor_endpoint(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_VENDOR, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS vendor_other(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_VENDOR, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS class_device(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_CLASS, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS class_interface(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_CLASS, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS class_endpoint(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_CLASS, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS class_other(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_CLASS, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS control_descriptor_request(vpdo_dev_t &vpdo, IRP *irp, URB &urb, bool dir_in, UCHAR recipient)
{
        PAGED_CODE();

        auto &r = urb.UrbControlDescriptorRequest;

        TraceUrb("%s: TransferBufferLength %lu(%#lx), Index %#x, %!usb_descriptor_type!, LanguageId %#04hx",
                urb_function_str(r.Hdr.Function), r.TransferBufferLength, r.TransferBufferLength, 
                r.Index, r.DescriptorType, r.LanguageId);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | 
                (dir_in ? USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT);

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, r.TransferBufferLength)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = UCHAR((dir_in ? USB_DIR_IN : USB_DIR_OUT) | USB_TYPE_STANDARD | recipient);
        pkt->bRequest = dir_in ? USB_REQUEST_GET_DESCRIPTOR : USB_REQUEST_SET_DESCRIPTOR;
        pkt->wValue.W = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(r.DescriptorType, r.Index);
        pkt->wIndex.W = r.LanguageId; // relevant for USB_STRING_DESCRIPTOR_TYPE only
        pkt->wLength = (USHORT)r.TransferBufferLength;

        return send(vpdo, irp, hdr, &urb);
}

PAGEABLE NTSTATUS get_descriptor_from_device(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb, bool(USB_DIR_IN), USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS set_descriptor_to_device(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb, bool(USB_DIR_OUT), USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS get_descriptor_from_interface(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb, bool(USB_DIR_IN), USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS set_descriptor_to_interface(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb,  bool(USB_DIR_OUT), USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS get_descriptor_from_endpoint(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb, bool(USB_DIR_IN), USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS set_descriptor_to_endpoint(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb, bool(USB_DIR_OUT), USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS control_feature_request(vpdo_dev_t &vpdo, IRP *irp, URB &urb, UCHAR bRequest, UCHAR recipient)
{
        PAGED_CODE();

        auto &r = urb.UrbControlFeatureRequest;

        TraceUrb("irp %04x -> %s: FeatureSelector %#hx, Index %#hx", 
                ptr4log(irp), urb_function_str(r.Hdr.Function), r.FeatureSelector, r.Index);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | recipient;
        pkt->bRequest = bRequest;
        pkt->wValue.W = r.FeatureSelector;
        pkt->wIndex.W = r.Index;

        return send(vpdo, irp, hdr);
}

PAGEABLE NTSTATUS set_feature_to_device(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_SET_FEATURE, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS set_feature_to_interface(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_SET_FEATURE, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS set_feature_to_endpoint(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_SET_FEATURE, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS set_feature_to_other(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb,  USB_REQUEST_SET_FEATURE, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS clear_feature_to_device(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS clear_feature_to_interface(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS clear_feature_to_endpoint(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS clear_feature_to_other(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_OTHER);
}

NTSTATUS select_configuration(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();

        auto &r = urb.UrbSelectConfiguration;
        auto cd = r.ConfigurationDescriptor; // nullptr if unconfigured

        UCHAR value = cd ? cd->bConfigurationValue : 0;
        TraceUrb("bConfigurationValue %d", value);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        pkt->bRequest = USB_REQUEST_SET_CONFIGURATION;
        pkt->wValue.W = value;

        return send(vpdo, irp, hdr);
}

NTSTATUS select_interface(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();

        auto &r = urb.UrbSelectInterface;
        auto &iface = r.Interface;

        TraceUrb("InterfaceNumber %d, AlternateSetting %d", iface.InterfaceNumber, iface.AlternateSetting);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE;
        pkt->bRequest = USB_REQUEST_SET_INTERFACE;
        pkt->wValue.W = iface.AlternateSetting;
        pkt->wIndex.W = iface.InterfaceNumber;

        return send(vpdo, irp, hdr);
}

/*
 * Can't be implemented without server's support.
 * In any case the result will be irrelevant due to network latency.
 * 
 * See: <linux>//drivers/usb/core/usb.c, usb_get_current_frame_number. 
 */
NTSTATUS get_current_frame_number(vpdo_dev_t&, IRP *irp, URB &urb)
{
	urb.UrbGetCurrentFrameNumber.FrameNumber = 0; // FIXME: get usb_get_current_frame_number() on Linux server
	TraceUrb("irp %04x: FrameNumber %lu", ptr4log(irp), urb.UrbGetCurrentFrameNumber.FrameNumber);

	urb.UrbHeader.Status = USBD_STATUS_SUCCESS;
	return STATUS_SUCCESS;
}

NTSTATUS control_transfer(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();

        static_assert(offsetof(_URB_CONTROL_TRANSFER, SetupPacket) == offsetof(_URB_CONTROL_TRANSFER_EX, SetupPacket));
        auto &r = urb.UrbControlTransferEx;

        {
                char buf_flags[USBD_TRANSFER_FLAGS_BUFBZ];
                char buf_setup[USB_SETUP_PKT_STR_BUFBZ];

                TraceUrb("irp %04x -> PipeHandle %#Ix, %s, TransferBufferLength %lu, Timeout %lu, %s",
                        ptr4log(irp), ph4log(r.PipeHandle),
                        usbd_transfer_flags(buf_flags, sizeof(buf_flags), r.TransferFlags),
                        r.TransferBufferLength,
                        urb.UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER_EX ? r.Timeout : 0,
                        usb_setup_pkt_str(buf_setup, sizeof(buf_setup), r.SetupPacket));
        }

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, r.PipeHandle, r.TransferFlags, r.TransferBufferLength)) {
                return err;
        }

        if (is_transfer_direction_out(&hdr) != is_transfer_dir_out(&urb.UrbControlTransfer)) { // TransferFlags can have wrong direction
                Trace(TRACE_LEVEL_ERROR, "Transfer direction differs in TransferFlags/PipeHandle and SetupPacket");
                return STATUS_INVALID_PARAMETER;
        }

        static_assert(sizeof(hdr.u.cmd_submit.setup) == sizeof(r.SetupPacket));
        RtlCopyMemory(hdr.u.cmd_submit.setup, r.SetupPacket, sizeof(hdr.u.cmd_submit.setup));

        return send(vpdo, irp, hdr, &urb, r.PipeHandle);
}

/*
 * PAGED_CODE() fails.
 * The USB bus driver processes this URB at DISPATCH_LEVEL.
 */
NTSTATUS bulk_or_interrupt_transfer(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        auto &r = urb.UrbBulkOrInterruptTransfer;

        {
                auto func = urb.UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL ? ", chained mdl" : " ";
                char buf[USBD_TRANSFER_FLAGS_BUFBZ];

                TraceUrb("irp %04x -> PipeHandle %#Ix, %s, TransferBufferLength %lu%s",
                         ptr4log(irp), ph4log(r.PipeHandle), usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags),
		         r.TransferBufferLength, func);
        }

        auto type = get_endpoint_type(r.PipeHandle);

        if (!(type == UsbdPipeTypeBulk || type == UsbdPipeTypeInterrupt)) {
                Trace(TRACE_LEVEL_ERROR, "%!USBD_PIPE_TYPE!", type);
                return STATUS_INVALID_PARAMETER;
        }

        auto ctx = alloc_send_context();
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        ctx->vpdo = &vpdo;
        ctx->irp = irp;

        if (auto err = set_cmd_submit_usbip_header(vpdo, ctx->hdr, r.PipeHandle, r.TransferFlags, r.TransferBufferLength)) {
                free(ctx);
                return err;
        }

        set_context(irp, ctx->hdr.base.seqnum, ST_NONE, r.PipeHandle);
        return async_send(urb, ctx);
}

/*
 * PAGED_CODE() fails.
 * USBD_START_ISO_TRANSFER_ASAP is appended because URB_GET_CURRENT_FRAME_NUMBER is not implemented.
 */
NTSTATUS isoch_transfer(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
	auto &r = urb.UrbIsochronousTransfer;

        {
                const char *func = urb.UrbHeader.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL ? ", chained mdl" : ".";
                char buf[USBD_TRANSFER_FLAGS_BUFBZ];
                TraceUrb("irp %04x -> PipeHandle %#Ix, %s, TransferBufferLength %lu, StartFrame %lu, NumberOfPackets %lu, ErrorCount %lu%s",
                        ptr4log(irp), ph4log(r.PipeHandle),	
                        usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags),
                        r.TransferBufferLength, 
                        r.StartFrame, 
                        r.NumberOfPackets, 
                        r.ErrorCount,
                        func);
        }

        auto type = get_endpoint_type(r.PipeHandle);

        if (type != UsbdPipeTypeIsochronous) {
                Trace(TRACE_LEVEL_ERROR, "%!USBD_PIPE_TYPE!", type);
                return STATUS_INVALID_PARAMETER;
        }

        auto ctx = alloc_send_context(r.NumberOfPackets);
        if (!ctx) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        ctx->vpdo = &vpdo;
        ctx->irp = irp;

        if (auto err = set_cmd_submit_usbip_header(vpdo, ctx->hdr, r.PipeHandle, 
                                                 r.TransferFlags | USBD_START_ISO_TRANSFER_ASAP, r.TransferBufferLength)) {
                free(ctx);
                return err;
        }

        if (auto err = repack(ctx->isoc, r)) {
                free(ctx);
                return err;
        }

        ctx->hdr.u.cmd_submit.start_frame = r.StartFrame;
        ctx->hdr.u.cmd_submit.number_of_packets = r.NumberOfPackets;

        set_context(irp, ctx->hdr.base.seqnum, ST_NONE, r.PipeHandle);
        return async_send(urb, ctx);
}

NTSTATUS function_deprecated(vpdo_dev_t&, IRP *irp, URB &urb)
{
	TraceUrb("irp %04x: %s not supported", ptr4log(irp), urb_function_str(urb.UrbHeader.Function));

	urb.UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS get_configuration(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();
        
        auto &r = urb.UrbControlGetConfigurationRequest;
        TraceUrb("irp %04x -> TransferBufferLength %lu (must be 1)", ptr4log(irp), r.TransferBufferLength);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, r.TransferBufferLength)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        pkt->bRequest = USB_REQUEST_GET_CONFIGURATION;
        pkt->wLength = (USHORT)r.TransferBufferLength; // must be 1

        return send(vpdo, irp, hdr, &urb);
}

NTSTATUS get_interface(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();
        
        auto &r = urb.UrbControlGetInterfaceRequest;

        TraceUrb("irp %04x -> TransferBufferLength %lu (must be 1), Interface %hu",
                ptr4log(irp), r.TransferBufferLength, r.Interface);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, r.TransferBufferLength)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE;
        pkt->bRequest = USB_REQUEST_GET_INTERFACE;
        pkt->wIndex.W = r.Interface;
        pkt->wLength = (USHORT)r.TransferBufferLength; // must be 1

        return send(vpdo, irp, hdr, &urb);
}

NTSTATUS get_ms_feature_descriptor(vpdo_dev_t&, IRP *irp, URB &urb)
{
	auto &r = urb.UrbOSFeatureDescriptorRequest;

	TraceUrb("irp %04x -> TransferBufferLength %lu, Recipient %d, InterfaceNumber %d, MS_PageIndex %d, MS_FeatureDescriptorIndex %d", 
                ptr4log(irp), r.TransferBufferLength, r.Recipient, r.InterfaceNumber, r.MS_PageIndex, r.MS_FeatureDescriptorIndex);

	return STATUS_NOT_SUPPORTED;
}

/*
 * See: <kernel>/drivers/usb/core/message.c, usb_set_isoch_delay.
 */
NTSTATUS get_isoch_pipe_transfer_path_delays(vpdo_dev_t&, IRP *irp, URB &urb)
{
	auto &r = urb.UrbGetIsochPipeTransferPathDelays;

	TraceUrb("irp %04x -> PipeHandle %#Ix, MaximumSendPathDelayInMilliSeconds %lu, MaximumCompletionPathDelayInMilliSeconds %lu",
                ptr4log(irp), ph4log(r.PipeHandle), 
		r.MaximumSendPathDelayInMilliSeconds, 
		r.MaximumCompletionPathDelayInMilliSeconds);

	return STATUS_NOT_SUPPORTED;
}

NTSTATUS open_static_streams(vpdo_dev_t&, IRP *irp, URB &urb)
{
	auto &r = urb.UrbOpenStaticStreams;

	TraceUrb("irp %04x -> PipeHandle %#Ix, NumberOfStreams %lu, StreamInfoVersion %hu, StreamInfoSize %hu",
                ptr4log(irp), ph4log(r.PipeHandle), r.NumberOfStreams, r.StreamInfoVersion, r.StreamInfoSize);

	return STATUS_NOT_SUPPORTED;
}

using urb_function_t = NTSTATUS (vpdo_dev_t&, IRP*, URB&);

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

/*
 * PAGED_CODE() fails.
 */
NTSTATUS usb_submit_urb(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
	auto func = urb.UrbHeader.Function;

	if (auto handler = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : nullptr) {
		return handler(vpdo, irp, urb);
	}

	Trace(TRACE_LEVEL_ERROR, "%s(%#04x) has no handler (reserved?)", urb_function_str(func), func);
	return STATUS_INVALID_PARAMETER;
}

auto setup_topology_address(vpdo_dev_t *vpdo, USB_TOPOLOGY_ADDRESS &r)
{
	r.RootHubPortNumber = static_cast<USHORT>(vpdo->port);
	NT_ASSERT(r.RootHubPortNumber == vpdo->port);

	TraceUrb("RootHubPortNumber %d", r.RootHubPortNumber);
	return STATUS_SUCCESS;
}

NTSTATUS usb_get_port_status(ULONG &status)
{
	status = USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
	TraceUrb("-> PORT_ENABLED|PORT_CONNECTED"); 
	return STATUS_SUCCESS;
}

/*
 * See: <linux>/drivers/usb/usbip/stub_rx.c, is_reset_device_cmd.
 */
PAGEABLE NTSTATUS usb_reset_port(vpdo_dev_t &vpdo, IRP *irp)
{
        PAGED_CODE();

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_RT_PORT; // USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER
        pkt->bRequest = USB_REQUEST_SET_FEATURE;
        pkt->wValue.W = USB_PORT_FEAT_RESET;

        return send(vpdo, irp, hdr);
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
void send_cmd_unlink(vpdo_dev_t &vpdo, IRP *irp)
{
        auto seqnum = get_seqnum(irp);
        TraceMsg("irp %04x, seqnum %u", ptr4log(irp), seqnum);

        if (auto sock = vpdo.sock) {
                usbip_header hdr{};
                set_cmd_unlink_usbip_header(vpdo, hdr, seqnum);
                if (auto err = usbip::send_cmd(sock, hdr)) {
                        Trace(TRACE_LEVEL_ERROR, "send_cmd %!STATUS!", err);
                }
        }

        auto old_status = InterlockedCompareExchange(get_status(irp), ST_IRP_CANCELED, ST_NONE);
        NT_ASSERT(old_status != ST_RECV_COMPLETE);

        if (old_status == ST_SEND_COMPLETE) {
                complete_canceled_irp(irp);
        }
}

extern "C" NTSTATUS vhci_internal_ioctl(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
//      NT_ASSERT(!irp->IoStatus.Information); // can fail

	auto irpstack = IoGetCurrentIrpStackLocation(irp);
	auto ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

        TraceDbg("Enter irql %!irql!, %s(%#08lX), irp %04x", 
		  KeGetCurrentIrql(), dbg_ioctl_code(ioctl_code), ioctl_code, ptr4log(irp));

        auto vpdo = to_vpdo_or_null(devobj);
	if (!vpdo) {
		Trace(TRACE_LEVEL_WARNING, "Internal ioctl is allowed for vpdo only");
		return complete_internal_ioctl(irp, STATUS_INVALID_DEVICE_REQUEST);
	} else if (vpdo->PnPState == pnp_state::Removed) {
                return complete_internal_ioctl(irp, STATUS_NO_SUCH_DEVICE);
        } else if (vpdo->unplugged) {
		return complete_internal_ioctl(irp, STATUS_DEVICE_NOT_CONNECTED);
	}

        auto status = STATUS_NOT_SUPPORTED;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = usb_submit_urb(*vpdo, irp, *static_cast<URB*>(URB_FROM_IRP(irp)));
		break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		status = usb_get_port_status(*(ULONG*)irpstack->Parameters.Others.Argument1);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = usb_reset_port(*vpdo, irp);
		break;
	case IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS:
		status = setup_topology_address(vpdo, *(USB_TOPOLOGY_ADDRESS*)irpstack->Parameters.Others.Argument1);
		break;
        default:
		Trace(TRACE_LEVEL_WARNING, "Unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	if (status == STATUS_PENDING) {
                TraceDbg("Leave %!STATUS!, irp %04x", status, ptr4log(irp));
	} else {
		complete_internal_ioctl(irp, status);
	}

	return status;
}
