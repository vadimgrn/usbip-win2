#include "vhci.h"
#include "trace.h"
#include "write.tmh"

#include "dbgcommon.h"
#include "usbip_proto.h"
#include "vpdo.h"
#include "vpdo_dsc.h"
#include "usbd_helper.h"
#include "irp.h"
#include "pdu.h"
#include "usbdsc.h"
#include "urbtransfer.h"
#include "csq.h"

namespace
{

inline auto& TRANSFERRED(IRP *irp) { return irp->IoStatus.Information; }
inline auto TRANSFERRED(const IRP *irp) { return irp->IoStatus.Information; }

inline auto get_irp_buffer(const IRP *irp)
{
	return irp->AssociatedIrp.SystemBuffer;
}

PAGEABLE auto get_irp_buffer_size(const IRP *irp)
{
	PAGED_CODE();
	auto irpstack = IoGetCurrentIrpStackLocation(const_cast<IRP*>(irp));
	return irpstack->Parameters.Write.Length;
}

PAGEABLE void *get_urb_buffer(void *buf, MDL *bufMDL)
{
	PAGED_CODE();

	if (buf) {
		return buf;
	}

	if (!bufMDL) {
		Trace(TRACE_LEVEL_ERROR, "TransferBuffer and TransferBufferMDL are NULL");
		return nullptr;
	}

	buf = MmGetSystemAddressForMdlSafe(bufMDL, NormalPagePriority | MdlMappingNoExecute);
	if (!buf) {
		Trace(TRACE_LEVEL_ERROR, "MmGetSystemAddressForMdlSafe error");
	}

	return buf;
}

/*
* Actual TransferBufferLength must already be set.
*/
PAGEABLE auto copy_to_transfer_buffer(URB *urb, const void *src)
{
	PAGED_CODE();

	bool mdl = urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL;
	NT_ASSERT(urb->UrbHeader.Function != URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL);

	auto r = AsUrbTransfer(urb);
	auto buf = get_urb_buffer(mdl ? nullptr : r->TransferBuffer, r->TransferBufferMDL);

	if (buf) {
		RtlCopyMemory(buf, src, r->TransferBufferLength);
	} else {
		r->TransferBufferLength = 0;
	}

	return buf;
}

PAGEABLE auto assign(ULONG &TransferBufferLength, int actual_length)
{
	PAGED_CODE();

	bool ok = actual_length >= 0 && (ULONG)actual_length <= TransferBufferLength;
	TransferBufferLength = ok ? actual_length : 0;

	return ok ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/*
* MmGetSystemAddressForMdlSafe is the most probable source of the failure of copy_to_transfer_buffer.
*/
constexpr auto get_copy_status(const void *p)
{
	return p ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

PAGEABLE NTSTATUS urb_function_generic(vpdo_dev_t*, URB *urb, const usbip_header *hdr)
{
	PAGED_CODE();

	auto r = AsUrbTransfer(urb);
	auto err = assign(r->TransferBufferLength, hdr->u.ret_submit.actual_length);

	if (err || is_transfer_direction_out(hdr)) { // TransferFlags can have wrong direction
		return err;
	}

	auto func = urb->UrbHeader.Function;
	bool log = func == URB_FUNCTION_CONTROL_TRANSFER || func == URB_FUNCTION_CONTROL_TRANSFER_EX; // don't expose sensitive data

	auto buf = copy_to_transfer_buffer(urb, hdr + 1);
	if (buf && log) {
		TraceUrb("%s(%#04x): %!BIN!", urb_function_str(func), func, WppBinary(buf, (USHORT)r->TransferBufferLength));
	}

	return get_copy_status(buf);
}

/*
 * EP0 stall is not an error, control endpoint cannot stall. 
 */
PAGEABLE NTSTATUS urb_select_configuration(vpdo_dev_t *vpdo, URB *urb, const usbip_header *hdr)
{
	PAGED_CODE();

	auto err = urb->UrbHeader.Status;

	if (err == EndpointStalled) {
		Trace(TRACE_LEVEL_WARNING, "Ignoring EP0 %s, usbip status %d", dbg_usbd_status(err), hdr->u.ret_submit.status);
		err = USBD_STATUS_SUCCESS;
	}

	return err ? STATUS_UNSUCCESSFUL : vpdo_select_config(vpdo, &urb->UrbSelectConfiguration);
}

/*
 * usb_set_interface can return -EPIPE, especially if a device's interface has only one altsetting.
 * 
 * Note that control and isochronous endpoints don't halt, although control
 * endpoints report "protocol stall" (for unsupported requests) using the
 * same status code used to report a true stall.
 *
 * See: drivers/usb/core/message.c, usb_set_interface, usb_clear_halt. 
 */
PAGEABLE NTSTATUS urb_select_interface(vpdo_dev_t *vpdo, URB *urb, const usbip_header *hdr)
{
	PAGED_CODE();

	auto err = urb->UrbHeader.Status;

	if (err == EndpointStalled) {
		auto ifnum = urb->UrbSelectInterface.Interface.InterfaceNumber;
		Trace(TRACE_LEVEL_WARNING, "Ignoring EP0 %s, usbip status %d, InterfaceNumber %d, num_altsetting %d", 
					dbg_usbd_status(err), hdr->u.ret_submit.status, ifnum,
					get_intf_num_altsetting(vpdo->actconfig, ifnum));

		err = USBD_STATUS_SUCCESS;
	}

	return err ? STATUS_UNSUCCESSFUL : vpdo_select_interface(vpdo, &urb->UrbSelectInterface);
}

/*
* A request can read descriptor header or full descriptor to obtain its real size.
* F.e. configuration descriptor is 9 bytes, but the full size is stored in wTotalLength.
*/
PAGEABLE NTSTATUS urb_control_descriptor_request(vpdo_dev_t *vpdo, URB *urb, const usbip_header *hdr)
{
	PAGED_CODE();

	auto &r = urb->UrbControlDescriptorRequest;

	auto err = assign(r.TransferBufferLength, hdr->u.ret_submit.actual_length);
	if (err) {
		return err;
	}

	switch (r.Hdr.Function) {
	case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
	case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:
	case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:
		return STATUS_SUCCESS; // USB_DIR_OUT
	}

	const USB_COMMON_DESCRIPTOR *dsc = nullptr;

	if (r.TransferBufferLength < sizeof(*dsc)) {
		Trace(TRACE_LEVEL_ERROR, "usb descriptor's header expected: TransferBufferLength(%lu)", r.TransferBufferLength);
		r.TransferBufferLength = 0;
		return STATUS_INVALID_PARAMETER;
	}

	dsc = (USB_COMMON_DESCRIPTOR*)copy_to_transfer_buffer(urb, hdr + 1);
	if (!dsc) {
		return get_copy_status(dsc);
	}

	TraceUrb("%s: bLength %d, %!usb_descriptor_type!, %!BIN!", 
			urb_function_str(r.Hdr.Function), 
			dsc->bLength,
			dsc->bDescriptorType,
			WppBinary(dsc, (USHORT)r.TransferBufferLength));

	USHORT dsc_len = dsc->bDescriptorType == USB_CONFIGURATION_DESCRIPTOR_TYPE ? 
			((USB_CONFIGURATION_DESCRIPTOR*)dsc)->wTotalLength : dsc->bLength;

	if (dsc_len > sizeof(*dsc) && dsc_len == r.TransferBufferLength) { // full descriptor
		cache_descriptor(vpdo, r, dsc);
	} else {
		TraceUrb("%s: skip caching of descriptor: TransferBufferLength(%lu), dsc_len(%d)", 
			urb_function_str(r.Hdr.Function), r.TransferBufferLength, dsc_len);
	}

	return STATUS_SUCCESS;
}

/*
* Buffer from the server has no gaps (compacted), SUM(src->actual_length) == src_len, 
* src->offset is ignored for that reason.
*
* For isochronous packets: actual length is the sum of
* the actual length of the individual, packets, but as
* the packet offsets are not changed there will be
* padding between the packets. To optimally use the
* bandwidth the padding is not transmitted.
*
* See: 
* <linux>/drivers/usb/usbip/stub_tx.c, stub_send_ret_submit
* <linux>/drivers/usb/usbip/usbip_common.c, usbip_pad_iso
*/
PAGEABLE NTSTATUS copy_isoc_data(
	_URB_ISOCH_TRANSFER &r, char *dst_buf, 
	const usbip_iso_packet_descriptor *src, const char *src_buf, ULONG src_len)
{
	PAGED_CODE();

	bool dir_out = !src_buf;
	auto dst = r.IsoPacket;

	ULONG src_offset = r.NumberOfPackets ? src->offset : 0;
	NT_ASSERT(!src_offset);

	for (ULONG i = 0; i < r.NumberOfPackets; ++i, ++src, ++dst) {

		dst->Status = src->status ? to_windows_status_isoch(src->status) : USBD_STATUS_SUCCESS;

		if (dir_out) {
			continue; // dst->Length not used for OUT transfers
		}

		if (!src->actual_length) {
			dst->Length = 0;
			continue;
		}

		if (src->actual_length > src->length) {
			Trace(TRACE_LEVEL_ERROR, "src->actual_length(%u) > src->length(%u)", src->actual_length, src->length);
			return STATUS_INVALID_PARAMETER;
		}

		if (src->offset != dst->Offset) { // buffer is compacted, but offsets are intact
			Trace(TRACE_LEVEL_ERROR, "src->offset(%u) != dst->Offset(%lu)", src->offset, dst->Offset);
			return STATUS_INVALID_PARAMETER;
		}

		if (src_offset > dst->Offset) {// source buffer has no gaps
			Trace(TRACE_LEVEL_ERROR, "src_offset(%lu) > dst->Offset(%lu)", src_offset, dst->Offset);
			return STATUS_INVALID_PARAMETER;
		}

		if (src_offset + src->actual_length > src_len) {
			Trace(TRACE_LEVEL_ERROR, "src_offset(%lu) + src->actual_length(%u) > src_len(%lu)", 
				src_offset, src->actual_length, src_len);

			return STATUS_INVALID_PARAMETER;
		}

		if (dst->Offset + src->actual_length > r.TransferBufferLength) {
			Trace(TRACE_LEVEL_ERROR, "dst->Offset(%lu) + src->actual_length(%u) > r.TransferBufferLength(%lu)", 
				dst->Offset, src->actual_length, r.TransferBufferLength);

			return STATUS_INVALID_PARAMETER;
		}

		RtlCopyMemory(dst_buf + dst->Offset, src_buf + src_offset, src->actual_length);

		dst->Length = src->actual_length;
		src_offset += src->actual_length;
	}

	bool ok = src_offset == src_len;
	if (!ok) {
		Trace(TRACE_LEVEL_ERROR, "src_offset(%lu) != src_len(%lu)", src_offset, src_len);
	}

	return ok ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/*
* Layout: usbip_header, data(IN only), usbip_iso_packet_descriptor[].
*/
PAGEABLE NTSTATUS urb_isoch_transfer(vpdo_dev_t *vpdo, URB *urb, const usbip_header *hdr)
{
	PAGED_CODE();

	auto &res = hdr->u.ret_submit;
	auto cnt = res.number_of_packets;

	auto &r = urb->UrbIsochronousTransfer;
	r.ErrorCount = res.error_count;

	if (cnt && cnt == res.error_count) {
		r.Hdr.Status = USBD_STATUS_ISOCH_REQUEST_FAILED;
	}

	if (r.TransferFlags & USBD_START_ISO_TRANSFER_ASAP) {
		r.StartFrame = res.start_frame;
	}

	if (!(cnt >= 0 && (ULONG)cnt == r.NumberOfPackets)) {
		Trace(TRACE_LEVEL_ERROR, "number_of_packets(%d) != NumberOfPackets(%lu)", cnt, r.NumberOfPackets);
		return STATUS_INVALID_PARAMETER;
	}

	if (!(res.actual_length >= 0 && (ULONG)res.actual_length <= r.TransferBufferLength)) {
		Trace(TRACE_LEVEL_ERROR, "actual_length(%d) > TransferBufferLength(%lu)", res.actual_length, r.TransferBufferLength);
		return STATUS_INVALID_PARAMETER;
	}

	vpdo->current_frame_number = res.start_frame;

	bool dir_in = is_transfer_direction_in(hdr); // TransferFlags can have wrong direction

	auto src_buf = (const char*)(hdr + 1);
	ULONG src_len = dir_in ? res.actual_length : 0;

	auto src = reinterpret_cast<const usbip_iso_packet_descriptor*>(src_buf + src_len);

	auto ptr = r.Hdr.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL ? nullptr : r.TransferBuffer;
	auto buf = (char*)get_urb_buffer(ptr, r.TransferBufferMDL);

	return buf ? copy_isoc_data(r, buf, src, dir_in ? src_buf : nullptr, src_len) : get_copy_status(buf);
}

/*
* Nothing to handle.
*/
PAGEABLE NTSTATUS urb_function_success(vpdo_dev_t*, URB*, const usbip_header*)
{
	PAGED_CODE();
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS urb_function_unexpected(vpdo_dev_t*, URB *urb, const usbip_header*)
{
	PAGED_CODE();

	auto func = urb->UrbHeader.Function;
	Trace(TRACE_LEVEL_ERROR, "%s(%#04x) must never be called, internal logic error", urb_function_str(func), func);

	return STATUS_INTERNAL_ERROR;
}	

using urb_function_t = NTSTATUS(vpdo_dev_t*, URB*, const usbip_header*);

urb_function_t* const urb_functions[] =
{
	urb_select_configuration,
	urb_select_interface,
	urb_function_unexpected, // URB_FUNCTION_ABORT_PIPE, urb_pipe_request

	urb_function_unexpected, // URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL
	urb_function_unexpected, // URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL

	urb_function_unexpected, // URB_FUNCTION_GET_FRAME_LENGTH
	urb_function_unexpected, // URB_FUNCTION_SET_FRAME_LENGTH
	urb_function_unexpected, // URB_FUNCTION_GET_CURRENT_FRAME_NUMBER

	urb_function_generic, // URB_FUNCTION_CONTROL_TRANSFER
	urb_function_generic, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER
	urb_isoch_transfer,

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE 

	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_DEVICE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_INTERFACE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_ENDPOINT, urb_control_feature_request

	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT, urb_control_feature_request

	urb_function_generic, // URB_FUNCTION_GET_STATUS_FROM_DEVICE
	urb_function_generic, // URB_FUNCTION_GET_STATUS_FROM_INTERFACE
	urb_function_generic, // URB_FUNCTION_GET_STATUS_FROM_ENDPOINT

	nullptr, // URB_FUNCTION_RESERVED_0X0016          

	urb_function_generic, // URB_FUNCTION_VENDOR_DEVICE
	urb_function_generic, // URB_FUNCTION_VENDOR_INTERFACE
	urb_function_generic, // URB_FUNCTION_VENDOR_ENDPOINT

	urb_function_generic, // URB_FUNCTION_CLASS_DEVICE 
	urb_function_generic, // URB_FUNCTION_CLASS_INTERFACE
	urb_function_generic, // URB_FUNCTION_CLASS_ENDPOINT

	nullptr, // URB_FUNCTION_RESERVE_0X001D

	urb_function_success, // URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL, urb_pipe_request

	urb_function_generic, // URB_FUNCTION_CLASS_OTHER
	urb_function_generic, // URB_FUNCTION_VENDOR_OTHER

	urb_function_generic, // URB_FUNCTION_GET_STATUS_FROM_OTHER

	urb_function_success, // URB_FUNCTION_SET_FEATURE_TO_OTHER, urb_control_feature_request
	urb_function_success, // URB_FUNCTION_CLEAR_FEATURE_TO_OTHER, urb_control_feature_request

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT

	urb_function_generic, // URB_FUNCTION_GET_CONFIGURATION
	urb_function_generic, // URB_FUNCTION_GET_INTERFACE

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE

	urb_function_unexpected, // URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR

	nullptr, // URB_FUNCTION_RESERVE_0X002B
	nullptr, // URB_FUNCTION_RESERVE_0X002C
	nullptr, // URB_FUNCTION_RESERVE_0X002D
	nullptr, // URB_FUNCTION_RESERVE_0X002E
	nullptr, // URB_FUNCTION_RESERVE_0X002F

	urb_function_unexpected, // URB_FUNCTION_SYNC_RESET_PIPE, urb_pipe_request
	urb_function_unexpected, // URB_FUNCTION_SYNC_CLEAR_STALL, urb_pipe_request
	urb_function_generic, // URB_FUNCTION_CONTROL_TRANSFER_EX

	nullptr, // URB_FUNCTION_RESERVE_0X0033
	nullptr, // URB_FUNCTION_RESERVE_0X0034                  

	urb_function_unexpected, // URB_FUNCTION_OPEN_STATIC_STREAMS
	urb_function_unexpected, // URB_FUNCTION_CLOSE_STATIC_STREAMS, urb_pipe_request
	urb_function_generic, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL
	urb_isoch_transfer, // URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL

	nullptr, // 0x0039
	nullptr, // 0x003A        
	nullptr, // 0x003B        
	nullptr, // 0x003C        

	urb_function_unexpected // URB_FUNCTION_GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS
};

/*
 * If response from a server has data (actual_length > 0), URB function MUST copy it to URB 
 * even if UrbHeader.Status != USBD_STATUS_SUCCESS.
 */
PAGEABLE NTSTATUS usb_submit_urb(vpdo_dev_t *vpdo, URB *urb, const usbip_header *hdr)
{
	PAGED_CODE(); 

	if (urb) {
		auto err = hdr->u.ret_submit.status;
		urb->UrbHeader.Status = err ? to_windows_status(err) : USBD_STATUS_SUCCESS;
	} else {
		Trace(TRACE_LEVEL_WARNING, "URB is null"); // FIXME: what is going on?
		return STATUS_INVALID_PARAMETER;
	}

	auto func = urb->UrbHeader.Function;
	auto pfunc = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : nullptr;

	auto err = pfunc ? pfunc(vpdo, urb, hdr) : STATUS_INVALID_PARAMETER;

	if (err && !urb->UrbHeader.Status) { // it's OK if (urb->UrbHeader.Status && !err)
		urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
		Trace(TRACE_LEVEL_VERBOSE, "Set USBD_STATUS=%s because return is %!STATUS!", dbg_usbd_status(urb->UrbHeader.Status), err);
	}

	return err;
}

PAGEABLE NTSTATUS get_descriptor_from_node_connection(IRP *irp, const usbip_header *hdr)
{
	PAGED_CODE();

	auto irpstack = IoGetCurrentIrpStackLocation(irp);
	auto data_sz = irpstack->Parameters.DeviceIoControl.OutputBufferLength; // r.Data[]

	auto actual_length = hdr->u.ret_submit.actual_length;

	if (!(actual_length >= 0 && (ULONG)actual_length <= data_sz)) {
		Trace(TRACE_LEVEL_ERROR, "OutputBufferLength %lu, actual_length(%d)", data_sz, actual_length);
		return STATUS_INVALID_PARAMETER;
	}

	auto r = (USB_DESCRIPTOR_REQUEST*)get_irp_buffer(irp);
	RtlCopyMemory(r->Data, hdr + 1, actual_length);

	auto uirp = reinterpret_cast<uintptr_t>(irp);

	TraceUrb("irp %04x <- ConnectionIndex %lu, OutputBufferLength %lu, actual_length %d, Data[%!BIN!]", 
			static_cast<UINT32>(uirp), r->ConnectionIndex, data_sz, actual_length, 
			WppBinary(r->Data, (USHORT)actual_length));

	auto err = to_windows_status(hdr->u.ret_submit.status);
	return !err || err == EndpointStalled ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

PAGEABLE NTSTATUS usb_reset_port(const usbip_header *hdr)
{
	PAGED_CODE();

	auto err = hdr->u.ret_submit.status;
	auto win_err = to_windows_status(err);

	if (win_err == EndpointStalled) { // control pipe stall is not an error, see urb_select_interface
		Trace(TRACE_LEVEL_WARNING, "Ignoring EP0 %s, usbip status %d", dbg_usbd_status(win_err), err);
		err = 0;
	}

	return err ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}

PAGEABLE NTSTATUS ret_submit(vpdo_dev_t *vpdo, IRP *irp, const usbip_header *hdr)
{
	PAGED_CODE();

	auto irpstack = IoGetCurrentIrpStackLocation(irp);
	auto ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	NTSTATUS st = STATUS_INVALID_PARAMETER;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		st = usb_submit_urb(vpdo, (URB*)URB_FROM_IRP(irp), hdr);
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		st = usb_reset_port(hdr);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		st = get_descriptor_from_node_connection(irp, hdr);
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "Unexpected IoControlCode %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	return st;
}

/*
 * status: This is similar to the status of USBIP_RET_SUBMIT (share the same memory offset).
 * When UNLINK is successful, status is -ECONNRESET;
 * when USBIP_CMD_UNLINK is after USBIP_RET_SUBMIT status is 0 
 * 
 * See: <kernel>/Documentation/usb/usbip_protocol.rst
 */
PAGEABLE NTSTATUS ret_unlink(const usbip_header *hdr)
{
	PAGED_CODE();
	
	TraceUrb("%s", dbg_usbd_status(to_windows_status(hdr->u.ret_unlink.status)));
	return STATUS_SUCCESS; // it's OK if can't unlink (too late, etc.)
}

PAGEABLE const usbip_header *consume_write_irp_buffer(IRP *irp)
{
	PAGED_CODE();

	auto hdr = (const usbip_header*)get_irp_buffer(irp);
	auto buf_sz = get_irp_buffer_size(irp);

	if (buf_sz < sizeof(*hdr)) {
		Trace(TRACE_LEVEL_ERROR, "usbip header expected: write buffer %lu", buf_sz);
		return nullptr;
	}

	auto pdu_sz = get_pdu_size(hdr);
	if (buf_sz != pdu_sz) {
		Trace(TRACE_LEVEL_ERROR, "Write buffer %lu != pdu size %Iu", buf_sz, pdu_sz);
		return nullptr;
	}

	TRANSFERRED(irp) = buf_sz;
	return hdr;
}

/* 
 * it seems windows client usb driver will think IoCompleteRequest is running at DISPATCH_LEVEL
 * so without this it will change IRQL sometimes, and introduce to a dead of my userspace program.
 */
void complete_request(IRP *irp, NTSTATUS status)
{
	irp->IoStatus.Status = status;
	TraceCall("%!STATUS!, Information %Iu, irp %p", status, irp->IoStatus.Information, irp);

	KIRQL irql;
	KeRaiseIrql(DISPATCH_LEVEL, &irql); // FIXME: really?
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	KeLowerIrql(irql);
}

PAGEABLE NTSTATUS process_write_irp(vpdo_dev_t *vpdo, IRP *write_irp)
{
	PAGED_CODE();

	auto hdr = consume_write_irp_buffer(write_irp);
	if (!hdr) {
		return STATUS_INVALID_PARAMETER;
	}

	auto irp = IoCsqRemoveNextIrp(&vpdo->tx_irps_csq, as_pointer(hdr->base.seqnum));

	if (irp) {
		auto uirp = reinterpret_cast<uintptr_t>(irp);
		char buf[DBG_USBIP_HDR_BUFSZ];
		TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "irp %04x <- %Iu%s", 
				static_cast<UINT32>(uirp), TRANSFERRED(write_irp), dbg_usbip_hdr(buf, sizeof(buf), hdr));
	} else {
		Trace(TRACE_LEVEL_VERBOSE, "Pending irp was cancelled, seqnum %u", hdr->base.seqnum);
		return STATUS_SUCCESS;
	}

	auto status = STATUS_INVALID_PARAMETER;

	switch (hdr->base.command) {
	case USBIP_RET_SUBMIT:
		status = ret_submit(vpdo, irp, hdr);
		break;
	case USBIP_RET_UNLINK:
		status = ret_unlink(hdr);
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "USBIP_RET_* expected, got %!usbip_request_type!", hdr->base.command);
	}

	complete_request(irp, status);
	return STATUS_SUCCESS;
}

} // namespace


/*
* WriteFile -> IRP_MJ_WRITE -> vhci_write
*/
extern "C" PAGEABLE NTSTATUS vhci_write(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
	PAGED_CODE();
	NT_ASSERT(!TRANSFERRED(irp));

	TraceCall("Enter irql %!irql!, write buffer %lu, irp %p", KeGetCurrentIrql(), get_irp_buffer_size(irp), irp);

	auto vhci = to_vhci_or_null(devobj);
	if (!vhci) {
		Trace(TRACE_LEVEL_WARNING, "Write for non-vhci is not allowed");
		return CompleteRequest(irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	NTSTATUS status = STATUS_NO_SUCH_DEVICE;

	if (vhci->PnPState != pnp_state::Removed) {
		auto irpstack = IoGetCurrentIrpStackLocation(irp);
		if (auto vpdo = static_cast<vpdo_dev_t*>(irpstack->FileObject->FsContext)) {
			status = vpdo->unplugged ? STATUS_DEVICE_NOT_CONNECTED : process_write_irp(vpdo, irp);
		}
	}

	NT_ASSERT(TRANSFERRED(irp) <= get_irp_buffer_size(irp));
	TraceCall("Leave %!STATUS!, transferred %Iu, irp %p", status, TRANSFERRED(irp), irp);

	return CompleteRequest(irp, status);
}
