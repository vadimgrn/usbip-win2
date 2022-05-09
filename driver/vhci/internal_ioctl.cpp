#include "internal_ioctl.h"
#include "trace.h"
#include "internal_ioctl.tmh"

#include "dev.h"
#include "dbgcommon.h"
#include "irp.h"
#include "csq.h"
#include "proto.h"
#include "ch9.h"
#include "ch11.h"
#include "usb_util.h"
#include "urbtransfer.h"
#include "usbd_helper.h"
#include "pdu.h"
#include "mdl_cpp.h"
#include "wsk_cpp.h"

namespace
{

inline auto& TRANSFERRED(IRP *irp) { return irp->IoStatus.Information; }
inline auto TRANSFERRED(const IRP *irp) { return irp->IoStatus.Information; }

inline auto get_irp_buffer(const IRP *irp)
{
        return irp->AssociatedIrp.SystemBuffer;
}

auto get_irp_buffer_size(const IRP *irp)
{
        auto irpstack = IoGetCurrentIrpStackLocation(const_cast<IRP*>(irp));
        return irpstack->Parameters.Read.Length;
}

auto try_get_irp_buffer(const IRP *irp, size_t min_size, [[maybe_unused]] bool unchecked = false)
{
        NT_ASSERT(unchecked || !TRANSFERRED(irp));

        auto sz = get_irp_buffer_size(irp);
        return sz >= min_size ? get_irp_buffer(irp) : nullptr;
}

inline auto get_usbip_header(const IRP *irp, bool unchecked = false)
{
        auto ptr = try_get_irp_buffer(irp, sizeof(usbip_header), unchecked);
        return static_cast<usbip_header*>(ptr);
}

const void *get_urb_buffer(void *buf, MDL *mdl)
{
        if (buf) {
                return buf;
        }

        if (!mdl) {
                Trace(TRACE_LEVEL_ERROR, "TransferBuffer and TransferBufferMDL are NULL");
                return nullptr;
        }

        buf = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute | MdlMappingNoWrite);
        if (!buf) {
                Trace(TRACE_LEVEL_ERROR, "MmGetSystemAddressForMdlSafe error");
        }

        return buf;
}

void debug(const usbip_header &hdr, const usbip::Mdl &mdl_hdr, const IRP *irp)
{
        auto pdu_sz = get_total_size(hdr);

        [[maybe_unused]] auto buf_sz = size(mdl_hdr);
        NT_ASSERT(buf_sz == sizeof(hdr) || (buf_sz > sizeof(hdr) && buf_sz == pdu_sz));

        char buf[DBG_USBIP_HDR_BUFSZ];
        TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "irp %04x -> %Iu%s", 
                ptr4log(irp), pdu_sz, dbg_usbip_hdr(buf, sizeof(buf), &hdr));
}

auto make_buffer_mdl(usbip::Mdl &mdl, URB &urb, usbip::Mdl &hdr)
{
        mdl.reset();

        auto r = AsUrbTransfer(&urb);
        if (!r->TransferBufferLength) { // should never happen
                return STATUS_INVALID_BUFFER_SIZE;
        }

        if (auto buf = r->TransferBuffer) {
                mdl = usbip::Mdl(usbip::memory::nonpaged, buf, r->TransferBufferLength);
                if (auto err = mdl.prepare_nonpaged()) {
                        mdl.reset();
                        return err;
                }
        } else if (!(r->TransferBufferMDL && MmGetMdlByteCount(r->TransferBufferMDL) == r->TransferBufferLength)) { // should never happen
                return STATUS_INVALID_PARAMETER;
        }

        hdr.next(mdl ? mdl.get() : r->TransferBufferMDL); // result
        return STATUS_SUCCESS;
}

auto assign(ULONG &TransferBufferLength, int actual_length)
{
        PAGED_CODE();

        bool ok = actual_length >= 0 && (ULONG)actual_length <= TransferBufferLength;
        TransferBufferLength = ok ? actual_length : 0;

        return ok ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

auto send_recv(vpdo_dev_t *vpdo, IRP *irp, URB &urb, usbip_header &hdr, bool dir_in)
{
        auto r = AsUrbTransfer(&urb);
        auto seqnum = hdr.base.seqnum;

        usbip::Mdl mdl_hdr(usbip::memory::stack, &hdr, sizeof(hdr));

        if (auto err = mdl_hdr.prepare(IoReadAccess)) {
                Trace(TRACE_LEVEL_ERROR, "Read prepare %!STATUS!", err);
                return err;
        }

        usbip::Mdl dummy;
        if (auto err = make_buffer_mdl(dummy, urb, mdl_hdr)) {
                Trace(TRACE_LEVEL_ERROR, "make_buffer_mdl %!STATUS!", err);
                return err;
        }

        auto mdl_data = mdl_hdr.next();
        if (dir_in) {
                mdl_hdr.next(nullptr);
        }

        debug(hdr, mdl_hdr, irp);
        byteswap_header(hdr, swap_dir::host2net);

        SIZE_T actual = 0;
        WSK_BUF buf{ mdl_hdr.get(), 0, size(mdl_hdr) };

        if (auto err = send(vpdo->sock, &buf, WSK_FLAG_NODELAY, actual)) {
                Trace(TRACE_LEVEL_ERROR, "Send %!STATUS!", err);
                return err;
        }

        if (actual != buf.Length) {
                Trace(TRACE_LEVEL_ERROR, "Send: actual %Iu != total %Iu", actual, buf.Length);
                return STATUS_UNSUCCESSFUL;
        }

        //

        mdl_hdr.unprepare();

        if (auto err = mdl_hdr.prepare(IoWriteAccess)) {
                Trace(TRACE_LEVEL_ERROR, "Write prepare %!STATUS!", err);
                return err;
        }

        mdl_hdr.next(nullptr);
        buf.Length = mdl_hdr.size();

        if (auto err = receive(vpdo->sock, &buf, WSK_FLAG_WAITALL, actual)) {
                Trace(TRACE_LEVEL_ERROR, "Receive usbip_header: %!STATUS!", err);
                return err;
        }

        if (actual != buf.Length) {
                Trace(TRACE_LEVEL_ERROR, "Receive usbip_header: actual %Iu != total %Iu", actual, buf.Length);
                return STATUS_UNSUCCESSFUL;
        }

        byteswap_header(hdr, swap_dir::net2host);

        auto &base = hdr.base;
        base.direction = dir_in ? USBIP_DIR_IN : USBIP_DIR_OUT; // restore

        {
                char buff[DBG_USBIP_HDR_BUFSZ];
                TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "irp %04x <- %Iu%s",
                        ptr4log(irp), get_total_size(hdr), dbg_usbip_hdr(buff, sizeof(buff), &hdr));
        }


        if (!(base.command == USBIP_RET_SUBMIT && base.seqnum == seqnum)) {
                Trace(TRACE_LEVEL_ERROR, "%!usbip_request_type!, seqnum %u", base.command, base.seqnum);
                return STATUS_UNSUCCESSFUL;
        }

        auto &ret_submit = hdr.u.ret_submit;

        {
                auto err = ret_submit.status;
                urb.UrbHeader.Status = err ? to_windows_status(err) : USBD_STATUS_SUCCESS;
        }

        {
                auto err = assign(r->TransferBufferLength, ret_submit.actual_length);
                if (err || !dir_in) { 
                        return err;
                }
        }

        buf.Mdl = mdl_data;
        buf.Length = MmGetMdlByteCount(mdl_data); // r->TransferBufferLength

        if (auto err = receive(vpdo->sock, &buf, WSK_FLAG_WAITALL, actual)) {
                Trace(TRACE_LEVEL_ERROR, "Receive data %!STATUS!", err);
                return err;
        }

        if (actual != buf.Length) {
                Trace(TRACE_LEVEL_ERROR, "Receive data: actual %Iu != total %Iu", actual, buf.Length);
                return STATUS_UNSUCCESSFUL;
        }

        TraceUrb("%!BIN!", WppBinary(MmGetSystemAddressForMdlSafe(mdl_data, LowPagePriority),
                static_cast<USHORT>(buf.Length)));

        return STATUS_SUCCESS;
}

/*
* PAGED_CODE() fails.
* USBD_ISO_PACKET_DESCRIPTOR.Length is not used (zero) for USB_DIR_OUT transfer.
*/
NTSTATUS do_copy_payload(void *dst_buf, const _URB_ISOCH_TRANSFER &r, ULONG *transferred)
{
        NT_ASSERT(dst_buf);

        *transferred = 0;
        bool mdl = r.Hdr.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL;

        auto src_buf = get_urb_buffer(mdl ? nullptr : r.TransferBuffer, r.TransferBufferMDL);
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

NTSTATUS do_copy_transfer_buffer(void *dst, const URB &urb, IRP *irp)
{
        NT_ASSERT(dst);

        bool mdl = urb.UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL;
        NT_ASSERT(urb.UrbHeader.Function != URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL);

        auto r = AsUrbTransfer(&urb);

        auto buf = get_urb_buffer(mdl ? nullptr : r->TransferBuffer, r->TransferBufferMDL);
        if (buf) {
                RtlCopyMemory(dst, buf, r->TransferBufferLength);
                TRANSFERRED(irp) += r->TransferBufferLength;
        }

        return buf ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

/*
 * PAGED_CODE() fails.
 */
NTSTATUS copy_payload(void *dst, IRP *irp, const _URB_ISOCH_TRANSFER &r, [[maybe_unused]] ULONG expected)
{
        ULONG transferred = 0;
        NTSTATUS err = do_copy_payload(dst, r, &transferred);

        if (!err) {
                NT_ASSERT(transferred == expected);
                TRANSFERRED(irp) += transferred;
        }

        return err;
}

/*
 * PAGED_CODE() fails.
 */
NTSTATUS copy_transfer_buffer(IRP *irp, const URB &urb, vpdo_dev_t*)
{
        auto r = AsUrbTransfer(&urb);
        NT_ASSERT(r->TransferBufferLength);

        auto buf_sz = get_irp_buffer_size(irp);
        auto transferred = (ULONG)TRANSFERRED(irp);

        NT_ASSERT(buf_sz >= transferred);

        if (buf_sz - transferred >= r->TransferBufferLength) {
                auto buf = (char*)get_irp_buffer(irp);
                return do_copy_transfer_buffer(buf + transferred, urb, irp);
        }

        return STATUS_SUCCESS;
}

/*
 * Copy usbip payload to read buffer, usbip_header was handled by previous IRP.
 * Userspace app reads usbip header (previous IRP), calculates usbip payload size, reads usbip payload (this IRP).
 */
PAGEABLE NTSTATUS transfer_payload(IRP *irp, URB &urb)
{
        PAGED_CODE();

        auto r = AsUrbTransfer(&urb);
        auto dst = try_get_irp_buffer(irp, r->TransferBufferLength);

        return dst ? do_copy_transfer_buffer(dst, urb, irp) : STATUS_BUFFER_TOO_SMALL;
}

PAGEABLE NTSTATUS urb_isoch_transfer_payload(IRP *irp, URB &urb)
{
        PAGED_CODE();

        auto &r = urb.UrbIsochronousTransfer;

        auto sz = get_payload_size(r);
        auto dst = try_get_irp_buffer(irp, sz);

        return dst ? copy_payload(dst, irp, r, sz) : STATUS_BUFFER_TOO_SMALL;
}

NTSTATUS abort_pipe(vpdo_dev_t *vpdo, USBD_PIPE_HANDLE PipeHandle)
{
	TraceUrb("PipeHandle %#Ix", ph4log(PipeHandle));

	if (!PipeHandle) {
		return STATUS_INVALID_PARAMETER;
	}

	auto ctx = make_peek_context(PipeHandle);

	while (auto irp = IoCsqRemoveNextIrp(&vpdo->tx_irps_csq, &ctx)) {
		send_cmd_unlink(vpdo, irp);
	}

	while (auto irp = IoCsqRemoveNextIrp(&vpdo->rx_irps_csq, &ctx)) {
		complete_canceled_irp(irp);
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
PAGEABLE NTSTATUS sync_reset_pipe_and_clear_stall(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();

        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto &r = urb.UrbPipeRequest;
        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, 0);
        if (err) {
                return err;
        }

        auto pkt = get_submit_setup(hdr);
        pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT;
        pkt->bRequest = USB_REQUEST_CLEAR_FEATURE;
        pkt->wValue.W = USB_FEATURE_ENDPOINT_STALL; // USB_ENDPOINT_HALT
        pkt->wIndex.W = get_endpoint_address(r.PipeHandle);

        TRANSFERRED(irp) = sizeof(*hdr);
        return STATUS_SUCCESS;
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
NTSTATUS pipe_request(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
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

PAGEABLE NTSTATUS control_get_status_request(vpdo_dev_t *vpdo, IRP *irp, URB &urb, UCHAR recipient)
{
        PAGED_CODE();

        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto &r = urb.UrbControlGetStatusRequest;

        TraceUrb("irp %04x -> %s: TransferBufferLength %lu (must be 2), Index %hd", 
                ptr4log(irp), urb_function_str(r.Hdr.Function), r.TransferBufferLength, r.Index);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;

        auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, r.TransferBufferLength);
        if (err) {
                return err;
        }

        auto pkt = get_submit_setup(hdr);
        pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | recipient;
        pkt->bRequest = USB_REQUEST_GET_STATUS;
        pkt->wIndex.W = r.Index;
        pkt->wLength = (USHORT)r.TransferBufferLength; // must be 2

        TRANSFERRED(irp) = sizeof(*hdr);
        return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS get_status_from_device(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_get_status_request(vpdo, irp, urb, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS get_status_from_interface(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_get_status_request(vpdo, irp, urb, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS get_status_from_endpoint(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_get_status_request(vpdo, irp, urb, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS get_status_from_other(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_get_status_request(vpdo, irp, urb, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS control_vendor_class_request(vpdo_dev_t *vpdo, IRP *irp, URB &urb, UCHAR type, UCHAR recipient)
{
        PAGED_CODE();

        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto &r = urb.UrbControlVendorClassRequest;

        {
                char buf[USBD_TRANSFER_FLAGS_BUFBZ];
                TraceUrb("irp %04x -> %s: %s, TransferBufferLength %lu, %s(%!#XBYTE!), Value %#hx, Index %#hx",
                        ptr4log(irp), urb_function_str(r.Hdr.Function), usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags), 
                        r.TransferBufferLength, brequest_str(r.Request), r.Request, r.Value, r.Index);
        }

        auto err = set_cmd_submit_usbip_header(vpdo, hdr, 
                EP0, r.TransferFlags | USBD_DEFAULT_PIPE_TRANSFER, r.TransferBufferLength);

        if (err) {
                return err;
        }

        bool dir_out = is_transfer_direction_out(hdr); // TransferFlags can have wrong direction

        auto pkt = get_submit_setup(hdr);
        pkt->bmRequestType.B = UCHAR((dir_out ? USB_DIR_OUT : USB_DIR_IN) | type | recipient);
        pkt->bRequest = r.Request;
        pkt->wValue.W = r.Value;
        pkt->wIndex.W = r.Index;
        pkt->wLength = (USHORT)r.TransferBufferLength;

        TRANSFERRED(irp) = sizeof(*hdr);

        if (dir_out && r.TransferBufferLength) {
                return copy_transfer_buffer(irp, urb, vpdo);
        }

        return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vendor_device(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_VENDOR, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS vendor_interface(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_VENDOR, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS vendor_endpoint(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_VENDOR, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS vendor_other(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_VENDOR, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS class_device(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_CLASS, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS class_interface(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_CLASS, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS class_endpoint(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_CLASS, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS class_other(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_vendor_class_request(vpdo, irp, urb, USB_TYPE_CLASS, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS control_descriptor_request(vpdo_dev_t *vpdo, IRP *irp, URB &urb, bool dir_in, UCHAR recipient)
{
        PAGED_CODE();

        auto &r = urb.UrbControlDescriptorRequest;

        TraceUrb("irp %04x -> %s: TransferBufferLength %lu(%#lx), Index %#x, %!usb_descriptor_type!, LanguageId %#04hx",
                ptr4log(irp), urb_function_str(r.Hdr.Function), r.TransferBufferLength, r.TransferBufferLength, 
                r.Index, r.DescriptorType, r.LanguageId);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | 
                (dir_in ? USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT);

        usbip_header hdr{};
        if (auto err = set_cmd_submit_usbip_header(vpdo, &hdr, EP0, TransferFlags, r.TransferBufferLength)) {
                return err;
        }

        auto pkt = get_submit_setup(&hdr);
        pkt->bmRequestType.B = UCHAR((dir_in ? USB_DIR_IN : USB_DIR_OUT) | USB_TYPE_STANDARD | recipient);
        pkt->bRequest = dir_in ? USB_REQUEST_GET_DESCRIPTOR : USB_REQUEST_SET_DESCRIPTOR;
        pkt->wValue.W = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(r.DescriptorType, r.Index);
        pkt->wIndex.W = r.LanguageId; // relevant for USB_STRING_DESCRIPTOR_TYPE only
        pkt->wLength = (USHORT)r.TransferBufferLength;

        return send_recv(vpdo, irp, urb, hdr, dir_in);
}

PAGEABLE NTSTATUS get_descriptor_from_device(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb, bool(USB_DIR_IN), USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS set_descriptor_to_device(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb, bool(USB_DIR_OUT), USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS get_descriptor_from_interface(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb, bool(USB_DIR_IN), USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS set_descriptor_to_interface(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb,  bool(USB_DIR_OUT), USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS get_descriptor_from_endpoint(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb, bool(USB_DIR_IN), USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS set_descriptor_to_endpoint(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_descriptor_request(vpdo, irp, urb, bool(USB_DIR_OUT), USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS control_feature_request(vpdo_dev_t *vpdo, IRP *irp, URB &urb, UCHAR bRequest, UCHAR recipient)
{
        PAGED_CODE();

        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto &r = urb.UrbControlFeatureRequest;

        TraceUrb("irp %04x -> %s: FeatureSelector %#hx, Index %#hx", 
                ptr4log(irp), urb_function_str(r.Hdr.Function), r.FeatureSelector, r.Index);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, 0);
        if (err) {
                return err;
        }

        auto pkt = get_submit_setup(hdr);
        pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | recipient;
        pkt->bRequest = bRequest;
        pkt->wValue.W = r.FeatureSelector;
        pkt->wIndex.W = r.Index;

        TRANSFERRED(irp) = sizeof(*hdr);
        return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS set_feature_to_device(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_SET_FEATURE, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS set_feature_to_interface(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_SET_FEATURE, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS set_feature_to_endpoint(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_SET_FEATURE, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS set_feature_to_other(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb,  USB_REQUEST_SET_FEATURE, USB_RECIP_OTHER);
}

PAGEABLE NTSTATUS clear_feature_to_device(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_DEVICE);
}

PAGEABLE NTSTATUS clear_feature_to_interface(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_INTERFACE);
}

PAGEABLE NTSTATUS clear_feature_to_endpoint(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_ENDPOINT);
}

PAGEABLE NTSTATUS clear_feature_to_other(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        return control_feature_request(vpdo, irp, urb, USB_REQUEST_CLEAR_FEATURE, USB_RECIP_OTHER);
}

NTSTATUS select_configuration(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();

        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto &r = urb.UrbSelectConfiguration;
        auto cd = r.ConfigurationDescriptor; // nullptr if unconfigured

        {
                char buf[SELECT_CONFIGURATION_STR_BUFSZ];
                TraceUrb("irp %04x -> %s", ptr4log(irp), select_configuration_str(buf, sizeof(buf), &r));
        }

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, 0);
        if (err) {
                return err;
        }

        auto pkt = get_submit_setup(hdr);
        pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        pkt->bRequest = USB_REQUEST_SET_CONFIGURATION;
        pkt->wValue.W = cd ? cd->bConfigurationValue : 0;

        TRANSFERRED(irp) = sizeof(*hdr);
        return STATUS_SUCCESS;
}

NTSTATUS select_interface(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();

        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto &r = urb.UrbSelectInterface;
        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        {
                char buf[SELECT_INTERFACE_STR_BUFSZ];
                TraceUrb("irp %04x -> %s", ptr4log(irp), select_interface_str(buf, sizeof(buf), &urb.UrbSelectInterface));
        }

        auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, 0);
        if (err) {
                return err;
        }

        auto pkt = get_submit_setup(hdr);
        pkt->bmRequestType.B = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE;
        pkt->bRequest = USB_REQUEST_SET_INTERFACE;
        pkt->wValue.W = r.Interface.AlternateSetting;
        pkt->wIndex.W = r.Interface.InterfaceNumber;

        TRANSFERRED(irp) = sizeof(*hdr);
        return  STATUS_SUCCESS;
}

/*
* Can't be implemented without server's support.
* In any case the result will be irrelevant due to network latency.
* 
* See: <linux>//drivers/usb/core/usb.c, usb_get_current_frame_number. 
*/
NTSTATUS get_current_frame_number(vpdo_dev_t*, IRP *irp, URB &urb)
{
	urb.UrbGetCurrentFrameNumber.FrameNumber = 0; // FIXME: get usb_get_current_frame_number() on Linux server
	TraceUrb("irp %04x: FrameNumber %lu", ptr4log(irp), urb.UrbGetCurrentFrameNumber.FrameNumber);

	urb.UrbHeader.Status = USBD_STATUS_SUCCESS;
	return STATUS_SUCCESS;
}

NTSTATUS control_transfer(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();

        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

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

        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, r.PipeHandle, r.TransferFlags, r.TransferBufferLength)) {
                return err;
        }

        set_pipe_handle(irp, r.PipeHandle);
        bool dir_out = is_transfer_direction_out(hdr); // TransferFlags can have wrong direction

        if (dir_out != is_transfer_dir_out(&urb.UrbControlTransfer)) {
                Trace(TRACE_LEVEL_ERROR, "Transfer direction differs in TransferFlags/PipeHandle and SetupPacket");
                return STATUS_INVALID_PARAMETER;
        }

        static_assert(sizeof(hdr->u.cmd_submit.setup) == sizeof(r.SetupPacket));
        RtlCopyMemory(hdr->u.cmd_submit.setup, r.SetupPacket, sizeof(hdr->u.cmd_submit.setup));

        TRANSFERRED(irp) = sizeof(*hdr);

        if (dir_out && r.TransferBufferLength) {
                return copy_transfer_buffer(irp, urb, vpdo);
        }

        return STATUS_SUCCESS;
}

/*
 * PAGED_CODE() fails.
 * The USB bus driver processes this URB at DISPATCH_LEVEL.
 */
NTSTATUS bulk_or_interrupt_transfer(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
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

        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto err = set_cmd_submit_usbip_header(vpdo, hdr, r.PipeHandle, r.TransferFlags, r.TransferBufferLength);
        if (err) {
                return err;
        }

        set_pipe_handle(irp, r.PipeHandle);
        TRANSFERRED(irp) = sizeof(*hdr);

        if (r.TransferBufferLength && is_transfer_direction_out(hdr)) { // TransferFlags can have wrong direction
                return copy_transfer_buffer(irp, urb, vpdo);
        }

        return STATUS_SUCCESS;
}

/*
 * PAGED_CODE() fails.
 * USBD_START_ISO_TRANSFER_ASAP is appended because _URB_GET_CURRENT_FRAME_NUMBER is not implemented.
 */
NTSTATUS isoch_transfer(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
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

        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto err = set_cmd_submit_usbip_header(vpdo, hdr, r.PipeHandle, 
                r.TransferFlags | USBD_START_ISO_TRANSFER_ASAP, r.TransferBufferLength);

        if (err) {
                return err;
        }

        set_pipe_handle(irp, r.PipeHandle);
        hdr->u.cmd_submit.start_frame = r.StartFrame;
        hdr->u.cmd_submit.number_of_packets = r.NumberOfPackets;

        TRANSFERRED(irp) = sizeof(*hdr);
        auto sz = get_payload_size(r);

        if (get_irp_buffer_size(irp) - TRANSFERRED(irp) >= sz) {
                return copy_payload(hdr + 1, irp, r, sz);
        }

        return STATUS_SUCCESS;
}

NTSTATUS function_deprecated(vpdo_dev_t*, IRP *irp, URB &urb)
{
	TraceUrb("irp %04x: %s not supported", ptr4log(irp), urb_function_str(urb.UrbHeader.Function));

	urb.UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS get_configuration(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();
        
        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto &r = urb.UrbControlGetConfigurationRequest;
        TraceUrb("irp %04x -> TransferBufferLength %lu (must be 1)", ptr4log(irp), r.TransferBufferLength);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;

        auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, r.TransferBufferLength);
        if (err) {
                return err;
        }

        auto pkt = get_submit_setup(hdr);
        pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        pkt->bRequest = USB_REQUEST_GET_CONFIGURATION;
        pkt->wLength = (USHORT)r.TransferBufferLength; // must be 1

        TRANSFERRED(irp) = sizeof(*hdr);
        return STATUS_SUCCESS;
}

NTSTATUS get_interface(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
{
        PAGED_CODE();
        
        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto &r = urb.UrbControlGetInterfaceRequest;

        TraceUrb("irp %04x -> TransferBufferLength %lu (must be 1), Interface %hu",
                ptr4log(irp), r.TransferBufferLength, r.Interface);

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_IN;

        if (auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, r.TransferBufferLength)) {
                return err;
        }

        auto pkt = get_submit_setup(hdr);
        pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE;
        pkt->bRequest = USB_REQUEST_GET_INTERFACE;
        pkt->wIndex.W = r.Interface;
        pkt->wLength = (USHORT)r.TransferBufferLength; // must be 1

        TRANSFERRED(irp) = sizeof(*hdr);
        return STATUS_SUCCESS;
}

NTSTATUS get_ms_feature_descriptor(vpdo_dev_t*, IRP *irp, URB &urb)
{
	auto &r = urb.UrbOSFeatureDescriptorRequest;

	TraceUrb("irp %04x -> TransferBufferLength %lu, Recipient %d, InterfaceNumber %d, MS_PageIndex %d, MS_FeatureDescriptorIndex %d", 
                ptr4log(irp), r.TransferBufferLength, r.Recipient, r.InterfaceNumber, r.MS_PageIndex, r.MS_FeatureDescriptorIndex);

	return STATUS_NOT_SUPPORTED;
}

/*
 * See: <kernel>/drivers/usb/core/message.c, usb_set_isoch_delay.
 */
NTSTATUS get_isoch_pipe_transfer_path_delays(vpdo_dev_t*, IRP *irp, URB &urb)
{
	auto &r = urb.UrbGetIsochPipeTransferPathDelays;

	TraceUrb("irp %04x -> PipeHandle %#Ix, MaximumSendPathDelayInMilliSeconds %lu, MaximumCompletionPathDelayInMilliSeconds %lu",
                ptr4log(irp), ph4log(r.PipeHandle), 
		r.MaximumSendPathDelayInMilliSeconds, 
		r.MaximumCompletionPathDelayInMilliSeconds);

	return STATUS_NOT_SUPPORTED;
}

NTSTATUS open_static_streams(vpdo_dev_t*, IRP *irp, URB &urb)
{
	auto &r = urb.UrbOpenStaticStreams;

	TraceUrb("irp %04x -> PipeHandle %#Ix, NumberOfStreams %lu, StreamInfoVersion %hu, StreamInfoSize %hu",
                ptr4log(irp), ph4log(r.PipeHandle), r.NumberOfStreams, r.StreamInfoVersion, r.StreamInfoSize);

	return STATUS_NOT_SUPPORTED;
}

using urb_function_t = NTSTATUS (vpdo_dev_t*, IRP*, URB&);

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
NTSTATUS usb_submit_urb(vpdo_dev_t *vpdo, IRP *irp, URB &urb)
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
PAGEABLE NTSTATUS get_descriptor_from_node_connection(vpdo_dev_t *vpdo, IRP *irp)
{
        PAGED_CODE();

        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        auto &r = *(const USB_DESCRIPTOR_REQUEST*)get_irp_buffer(irp);

        auto irpstack = IoGetCurrentIrpStackLocation(irp);
        auto data_sz = irpstack->Parameters.DeviceIoControl.OutputBufferLength; // length of r.Data[]

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN;

        auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, data_sz);
        if (err) {
                return err;
        }

        auto pkt = get_submit_setup(hdr);
        pkt->bmRequestType.B = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        pkt->bRequest = USB_REQUEST_GET_DESCRIPTOR;
        pkt->wValue.W = r.SetupPacket.wValue;
        pkt->wIndex.W = r.SetupPacket.wIndex;
        pkt->wLength = r.SetupPacket.wLength;

        char buf[USB_SETUP_PKT_STR_BUFBZ];
        TraceUrb("ConnectionIndex %lu, %s", r.ConnectionIndex, usb_setup_pkt_str(buf, sizeof(buf), &r.SetupPacket));

        TRANSFERRED(irp) = sizeof(*hdr);
        return STATUS_SUCCESS;
}

/*
 * See: <linux>/drivers/usb/usbip/stub_rx.c, is_reset_device_cmd.
 */
PAGEABLE NTSTATUS usb_reset_port(vpdo_dev_t *vpdo, IRP *irp)
{
        PAGED_CODE();

        auto hdr = get_usbip_header(irp);
        if (!hdr) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        const ULONG TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;

        auto err = set_cmd_submit_usbip_header(vpdo, hdr, EP0, TransferFlags, 0);
        if (err) {
                return err;
        }

        auto pkt = get_submit_setup(hdr);
        pkt->bmRequestType.B = USB_RT_PORT; // USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER
        pkt->bRequest = USB_REQUEST_SET_FEATURE;
        pkt->wValue.W = USB_PORT_FEAT_RESET;

        TRANSFERRED(irp) = sizeof(*hdr);
        return STATUS_SUCCESS;
}

/*
 * PAGED_CODE() fails.
 */
NTSTATUS cmd_unlink(vpdo_dev_t *vpdo, IRP *irp, seqnum_t seqnum_unlink)
{
        if (auto hdr = get_usbip_header(irp)) {
                set_cmd_unlink_usbip_header(vpdo, hdr, seqnum_unlink);
                TRANSFERRED(irp) += sizeof(*hdr);
                return STATUS_SUCCESS;
        }

        return STATUS_INVALID_PARAMETER;
}

auto complete_internal_ioctl(IRP *irp, NTSTATUS status)
{
        TraceCall("irp %04x %!STATUS!", ptr4log(irp), status);
        NT_ASSERT(!irp->IoStatus.Information);
        return CompleteRequest(irp, status);
}

} // namespace


NTSTATUS send_to_server(vpdo_dev_t*, IRP*) 
{
        Trace(TRACE_LEVEL_ERROR, "NOT_IMPLEMENTED");
        return STATUS_NOT_IMPLEMENTED; 
}

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
 * IRP will be cancelled as soon as read thread will dequeue it from rx_irps_unlink and CMD_UNLINK will be issued.
 * RET_SUBMIT and RET_INLINK must be ignored if IRP is not found (IRP was cancelled and completed).
 */
NTSTATUS send_cmd_unlink(vpdo_dev_t *vpdo, IRP *irp)
{
        auto seqnum = get_seqnum(irp);
        NT_ASSERT(seqnum);

        TraceCall("irp %04x, unlink seqnum %u", ptr4log(irp), seqnum);

        set_seqnum_unlink(irp, seqnum);
        send_to_server(vpdo, irp);

        return STATUS_SUCCESS;
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
		status = usb_submit_urb(vpdo, irp, *static_cast<URB*>(buffer));
		break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		status = usb_get_port_status(*static_cast<ULONG*>(buffer));
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = usb_reset_port(vpdo, irp);
		break;
	case IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS:
		status = setup_topology_address(vpdo, *static_cast<USB_TOPOLOGY_ADDRESS*>(buffer));
		break;
        case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
                status = get_descriptor_from_node_connection(vpdo, irp);
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

