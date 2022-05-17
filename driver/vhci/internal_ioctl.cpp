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

namespace
{

/*
 * IRP can be canceled only if STATUS_PENDING is returned from IRP_MJ_INTERNAL_DEVICE_CONTROL.
 */
auto send_to_server(_Inout_ vpdo_dev_t &vpdo, _Inout_ IRP *irp, 
        _Inout_ usbip_header &hdr, _Inout_opt_ const URB *transfer_buffer = nullptr)
{
        clear_context(irp, false);
        set_seqnum(irp, hdr.base.seqnum);

        if (auto err = IoCsqInsertIrpEx(&vpdo.irps_csq, irp, nullptr, InsertTail())) {
                return err;
        }

        if (auto err = usbip::send_cmd(vpdo.sock, hdr, transfer_buffer)) {
                NT_VERIFY(dequeue_irp(vpdo, hdr.base.seqnum));
                return err;
        }

        return STATUS_PENDING;
}

/*
 * PAGED_CODE() fails.
 * USBD_ISO_PACKET_DESCRIPTOR.Length is not used (zero) for USB_DIR_OUT transfer.
 */
NTSTATUS do_copy_payload(void *dst_buf, const _URB_ISOCH_TRANSFER &r, ULONG *transferred)
{
        NT_ASSERT(dst_buf);

        *transferred = 0;
//        bool mdl = r.Hdr.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL;

        void *src_buf = nullptr;// usbip::get_urb_buffer(mdl ? nullptr : r.TransferBuffer, r.TransferBufferMDL, true);
        if (!src_buf) {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        auto buf_sz = is_endpoint_direction_out(r.PipeHandle) ? r.TransferBufferLength : 0; // TransferFlags can have wrong direction

        RtlCopyMemory(dst_buf, src_buf, buf_sz);
        *transferred += buf_sz;

        auto dsc = reinterpret_cast<usbip_iso_packet_descriptor*>((char*)dst_buf + buf_sz);
        ULONG sum = 0;

        for (ULONG i = 0; i < r.NumberOfPackets; ++dsc) {

                auto offset = r.IsoPacket[i].Offset;
                auto next_offset = ++i < r.NumberOfPackets ? r.IsoPacket[i].Offset : r.TransferBufferLength;

                if (next_offset >= offset && next_offset <= r.TransferBufferLength) {
                        dsc->offset = offset;
                        dsc->length = next_offset - offset;
                        dsc->actual_length = 0;
                        dsc->status = 0;
                        sum += dsc->length;
                } else {
                        Trace(TRACE_LEVEL_ERROR, "[%lu] next_offset(%lu) >= offset(%lu) && next_offset <= r.TransferBufferLength(%lu)",
                                i, next_offset, offset, r.TransferBufferLength);

                        return STATUS_INVALID_PARAMETER;
                }
        }

        *transferred += r.NumberOfPackets*sizeof(*dsc);

        NT_ASSERT(sum == r.TransferBufferLength);
        return STATUS_SUCCESS;
}

/*
 * PAGED_CODE() fails.
 */
auto get_payload_size(const _URB_ISOCH_TRANSFER &r)
{
        ULONG len = r.NumberOfPackets*sizeof(usbip_iso_packet_descriptor);

        if (is_endpoint_direction_out(r.PipeHandle)) {
                len += r.TransferBufferLength;
        }

        return len;
}

/*
 * PAGED_CODE() fails.
 */
NTSTATUS copy_payload(void *dst, IRP*, const _URB_ISOCH_TRANSFER &r, [[maybe_unused]] ULONG expected)
{
        ULONG transferred = 0;
        NTSTATUS err = do_copy_payload(dst, r, &transferred);

        if (!err) {
                NT_ASSERT(transferred == expected);
        }

        return err;
}

PAGEABLE NTSTATUS urb_isoch_transfer_payload(IRP *irp, URB &urb)
{
        PAGED_CODE();

        auto &r = urb.UrbIsochronousTransfer;
        auto sz = get_payload_size(r);
        auto dst = nullptr;
        return dst ? copy_payload(dst, irp, r, sz) : STATUS_BUFFER_TOO_SMALL;
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

        return send_to_server(vpdo, irp, hdr);
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

        return send_to_server(vpdo, irp, hdr, &urb);
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

        return send_to_server(vpdo, irp, hdr, &urb);
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

        return send_to_server(vpdo, irp, hdr, &urb);
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

        return send_to_server(vpdo, irp, hdr);
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

        {
                char buf[SELECT_CONFIGURATION_STR_BUFSZ];
                TraceUrb("irp %04x -> %s", ptr4log(irp), select_configuration_str(buf, sizeof(buf), &r));
        }

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        pkt->bRequest = USB_REQUEST_SET_CONFIGURATION;
        pkt->wValue.W = cd ? cd->bConfigurationValue : 0;

        return send_to_server(vpdo, irp, hdr);
}

NTSTATUS select_interface(vpdo_dev_t &vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();

        auto &r = urb.UrbSelectInterface;
        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        {
                char buf[SELECT_INTERFACE_STR_BUFSZ];
                TraceUrb("irp %04x -> %s", ptr4log(irp), select_interface_str(buf, sizeof(buf), &urb.UrbSelectInterface));
        }

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE;
        pkt->bRequest = USB_REQUEST_SET_INTERFACE;
        pkt->wValue.W = r.Interface.AlternateSetting;
        pkt->wIndex.W = r.Interface.InterfaceNumber;

        return send_to_server(vpdo, irp, hdr);
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

        set_pipe_handle(irp, r.PipeHandle);

        if (is_transfer_direction_out(&hdr) != is_transfer_dir_out(&urb.UrbControlTransfer)) { // TransferFlags can have wrong direction
                Trace(TRACE_LEVEL_ERROR, "Transfer direction differs in TransferFlags/PipeHandle and SetupPacket");
                return STATUS_INVALID_PARAMETER;
        }

        static_assert(sizeof(hdr.u.cmd_submit.setup) == sizeof(r.SetupPacket));
        RtlCopyMemory(hdr.u.cmd_submit.setup, r.SetupPacket, sizeof(hdr.u.cmd_submit.setup));

        return send_to_server(vpdo, irp, hdr, &urb);
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
                        ptr4log(irp), ph4log(r.PipeHandle),
		        usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags),
		        r.TransferBufferLength,
		        func);
        }

        auto type = get_endpoint_type(r.PipeHandle);

        if (!(type == UsbdPipeTypeBulk || type == UsbdPipeTypeInterrupt)) {
                Trace(TRACE_LEVEL_ERROR, "%!USBD_PIPE_TYPE!", type);
                return STATUS_INVALID_PARAMETER;
        }

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, r.PipeHandle, r.TransferFlags, r.TransferBufferLength)) {
                return err;
        }

        set_pipe_handle(irp, r.PipeHandle);

        if (KeGetCurrentIrql() >= DISPATCH_LEVEL) {
                return STATUS_INVALID_LEVEL;
        }

        return send_to_server(vpdo, irp, hdr, &urb);
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

        usbip_header hdr{};
        auto err = set_cmd_submit_usbip_header(vpdo, hdr, r.PipeHandle, r.TransferFlags | USBD_START_ISO_TRANSFER_ASAP, 
                                               r.TransferBufferLength);

        if (err) {
                return err;
        }

        set_pipe_handle(irp, r.PipeHandle);

        hdr.u.cmd_submit.start_frame = r.StartFrame;
        hdr.u.cmd_submit.number_of_packets = r.NumberOfPackets;

        return STATUS_NOT_IMPLEMENTED;
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

        return send_to_server(vpdo, irp, hdr, &urb);
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

        return send_to_server(vpdo, irp, hdr, &urb);
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
 * vhci_ioctl -> vhci_ioctl_vhub -> get_descriptor_from_nodeconn -> vpdo_get_dsc_from_nodeconn -> req_fetch_dsc -> submit_urbr -> vhci_read
 */
PAGEABLE NTSTATUS get_descriptor_from_node_connection(vpdo_dev_t &vpdo, IRP *irp)
{
        PAGED_CODE();

        auto &r = *reinterpret_cast<const USB_DESCRIPTOR_REQUEST*>(irp->AssociatedIrp.SystemBuffer);

        auto irpstack = IoGetCurrentIrpStackLocation(irp);
        auto data_sz = irpstack->Parameters.DeviceIoControl.OutputBufferLength; // length of r.Data[]

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN;

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, data_sz)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        pkt->bRequest = USB_REQUEST_GET_DESCRIPTOR;
        pkt->wValue.W = r.SetupPacket.wValue;
        pkt->wIndex.W = r.SetupPacket.wIndex;
        pkt->wLength = r.SetupPacket.wLength;

        char buf[USB_SETUP_PKT_STR_BUFBZ];
        TraceUrb("ConnectionIndex %lu, %s", r.ConnectionIndex, usb_setup_pkt_str(buf, sizeof(buf), &r.SetupPacket));

        return send_to_server(vpdo, irp, hdr);
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

        return send_to_server(vpdo, irp, hdr);
}

auto complete_internal_ioctl(IRP *irp, NTSTATUS status)
{
        TraceCall("irp %04x %!STATUS!", ptr4log(irp), status);
        NT_ASSERT(!irp->IoStatus.Information);
        return CompleteRequest(irp, status);
}

} // namespace


/*
 * There is a race condition between RET_SUBMIT and CMD_UNLINK.
 * Sequence of events:
 * 1.Pending IRPs are waiting for RET_SUBMIT in tx_irps.
 * 2.An upper driver cancels IRP.
 * 3.IRP is removed from tx_irps, CsqCompleteCanceledIrp callback is called.
 * 4.IRP is inserted into rx_irps_unlink (waiting for read IRP).
 * 5.IRP is dequeued from rx_irps_unlink and appended into tx_irps_unlink atomically.
 * 6.CMD_UNLINK is issued.
 * 
 * RET_SUBMIT can be received
 * a)Before #3 - normal case, IRP will be dequeued from tx_irps.
 * b)Between #3 and #4, IRP will not be found.
 * c)Between #4 and #5, IRP will be dequeued from rx_irps_unlink.
 * d)After #5, IRP will be dequeued from tx_irps_unlink.
 * 
 * Case b) is unavoidable because CSQ library calls CsqCompleteCanceledIrp after releasing a lock.
 * For that reason the cancellation logic is simplified and tx_irps_unlink is removed.
 * IRP will be completed as soon as read thread will dequeue it from rx_irps_unlink and CMD_UNLINK will be issued.
 * RET_SUBMIT and RET_INLINK must be ignored if IRP is not found (IRP was cancelled and completed).
 */
void send_cmd_unlink(vpdo_dev_t &vpdo, IRP *irp)
{
        auto seqnum = get_seqnum(irp);
        NT_ASSERT(extract_num(seqnum));

        TraceCall("irp %04x, unlink seqnum %u", ptr4log(irp), seqnum);
        set_seqnum_unlink(irp, seqnum);

        usbip_header hdr{};
        set_cmd_unlink_usbip_header(vpdo, hdr, seqnum);

        if (auto err = usbip::send_cmd(vpdo.sock, hdr)) {
                Trace(TRACE_LEVEL_ERROR, "send_cmd %!STATUS!", err);
        }

        complete_canceled_irp(irp);
}

extern "C" NTSTATUS vhci_internal_ioctl(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
        NT_ASSERT(!irp->IoStatus.Information);

	auto irpstack = IoGetCurrentIrpStackLocation(irp);
	auto ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	TraceCall("Enter irql %!irql!, %s(%#08lX), irp %04x", 
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

        auto buffer = URB_FROM_IRP(irp); // irpstack->Parameters.Others.Argument1
        if (!buffer) {
                Trace(TRACE_LEVEL_ERROR, "Buffer is NULL");
                return complete_internal_ioctl(irp, STATUS_INVALID_PARAMETER);
        }

        auto status = STATUS_NOT_SUPPORTED;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = usb_submit_urb(*vpdo, irp, *static_cast<URB*>(buffer));
		break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		status = usb_get_port_status(*static_cast<ULONG*>(buffer));
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = usb_reset_port(*vpdo, irp);
		break;
	case IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS:
		status = setup_topology_address(vpdo, *static_cast<USB_TOPOLOGY_ADDRESS*>(buffer));
		break;
        case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
                status = get_descriptor_from_node_connection(*vpdo, irp);
                break;
        default:
		Trace(TRACE_LEVEL_WARNING, "Unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	if (status == STATUS_PENDING) {
		TraceCall("Leave %!STATUS!, irp %04x", status, ptr4log(irp));
	} else {
		complete_internal_ioctl(irp, status);
	}

	return status;
}
