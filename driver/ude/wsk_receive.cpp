﻿/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wsk_receive.h"
#include "context.h"
#include "trace.h"
#include "wsk_receive.tmh"

#include "context.h"
#include "wsk_context.h"
#include "device.h"
#include "device_queue.h"
#include "urbtransfer.h"
#include "network.h"
#include "driver.h"
#include "ioctl.h"

#include <libdrv\usbd_helper.h>
#include <libdrv\dbgcommon.h>
#include <libdrv\wsk_cpp.h>
#include <libdrv\pdu.h>

#include <usb.h>

namespace
{

using namespace usbip;

using PWSK_CONTEXT = wsk_context*;
WDF_DECLARE_CONTEXT_TYPE(PWSK_CONTEXT); // WdfObjectGet_PWSK_CONTEXT

inline auto& get_wsk_context(_In_ WDFWORKITEM wi)
{
	return *WdfObjectGet_PWSK_CONTEXT(wi);
}

_Function_class_(EVT_WDF_OBJECT_CONTEXT_DESTROY)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void NTAPI workitem_destroy(_In_ WDFOBJECT Object)
{
	PAGED_CODE();

	auto wi = static_cast<WDFWORKITEM>(Object);
	TraceDbg("%04x", ptr04x(wi));

	if (auto ctx = get_wsk_context(wi)) {
		free(ctx, true);
	}
}

constexpr auto check(_In_ ULONG TransferBufferLength, _In_ int actual_length)
{
	return  actual_length >= 0 && ULONG(actual_length) <= TransferBufferLength ? 
		STATUS_SUCCESS : STATUS_INVALID_BUFFER_SIZE;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto assign(_Inout_ ULONG &TransferBufferLength, _In_ int actual_length)
{
	auto err = check(TransferBufferLength, actual_length);
	TransferBufferLength = err ? 0 : actual_length;
	return err;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto& get_ret_submit(_In_ const wsk_context &ctx)
{
	auto &hdr = ctx.hdr;
	NT_ASSERT(hdr.base.command == USBIP_RET_SUBMIT);
	return hdr.u.ret_submit;
}

/*
 * Buffer from the server has no gaps (compacted), SUM(src->actual_length) == actual_length,
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
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto fill_isoc_data(_Inout_ _URB_ISOCH_TRANSFER &r, _Inout_ char *buffer, _In_ ULONG length, 
	_In_ const usbip_iso_packet_descriptor* sd)
{
	auto dir_out = !buffer;
	auto sd_offset = length;

	auto dd = r.IsoPacket + r.NumberOfPackets - 1;
	sd += r.NumberOfPackets - 1;

	for (auto i = r.NumberOfPackets; i; --i, --sd, --dd) { // set dd.Status and dd.Length

		dd->Status = sd->status ? to_windows_status_isoch(sd->status) : USBD_STATUS_SUCCESS;

		if (dir_out) {
			continue; // dd->Length is not used for OUT transfers
		}

		if (!sd->actual_length) {
			dd->Length = 0;
			continue;
		}

		if (sd->actual_length > sd->length) {
			Trace(TRACE_LEVEL_ERROR, "actual_length(%u) > length(%u)", sd->actual_length, sd->length);
			return STATUS_INVALID_PARAMETER;
		}

		if (sd->offset != dd->Offset) { // buffer is compacted, but offsets are intact
			Trace(TRACE_LEVEL_ERROR, "src.offset(%u) != dst.Offset(%lu)", sd->offset, dd->Offset);
			return STATUS_INVALID_PARAMETER;
		}

		if (sd_offset >= sd->actual_length) {
			sd_offset -= sd->actual_length;
		} else {
			Trace(TRACE_LEVEL_ERROR, "sd_offset(%lu) >= actual_length(%u)", sd_offset, sd->actual_length);
			return STATUS_INVALID_PARAMETER;
		}

		if (sd_offset > dd->Offset) {// source buffer has no gaps
			Trace(TRACE_LEVEL_ERROR, "sd_offset(%lu) > dst.Offset(%lu)", sd_offset, dd->Offset);
			return STATUS_INVALID_PARAMETER;
		}

		if (sd_offset + sd->actual_length > length) {
			Trace(TRACE_LEVEL_ERROR, "sd_offset(%lu) + actual_length(%u) > length(%lu)", 
				sd_offset, sd->actual_length, length);
			return STATUS_INVALID_PARAMETER;
		}

		if (dd->Offset + sd->actual_length > r.TransferBufferLength) {
			Trace(TRACE_LEVEL_ERROR, "dst.Offset(%lu) + src.actual_length(%u) > r.TransferBufferLength(%lu)",
				dd->Offset, sd->actual_length, r.TransferBufferLength);
			return STATUS_INVALID_PARAMETER;
		}

		if (dd->Offset > sd_offset) {
			RtlMoveMemory(buffer + dd->Offset, buffer + sd_offset, sd->actual_length);
		} else { // buffer is filled without gaps from the beginning
			NT_ASSERT(dd->Offset == sd_offset);
		}

		dd->Length = sd->actual_length;
	}

	if (!dir_out && sd_offset) {
		Trace(TRACE_LEVEL_ERROR, "SUM(actual_length) != actual_length(%lu), delta is %lu", length, sd_offset);
		return STATUS_INVALID_PARAMETER; 
	}

	return STATUS_SUCCESS;
}

/*
 * ctx.mdl_buf can't be used, it describes actual_length instead of TransferBufferLength.
 * Try TransferBufferMDL first because it is locked-down and to obey URB_FUNCTION_XXX_USING_CHAINED_MDL.
 * 
 * If BSOD happen, this call should be used
 * make_transfer_buffer_mdl(ctx.mdl_buf, URB_BUF_LEN, false, IoModifyAccess, urb)
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto get_transfer_buffer(_In_ void *TransferBuffer, _In_ MDL *TransferBufferMDL)
{
	if (auto buf = TransferBufferMDL) {
		return MmGetSystemAddressForMdlSafe(buf, LowPagePriority | MdlMappingNoExecute);
	}

	NT_ASSERT(TransferBuffer); // make_transfer_buffer_mdl checks it before payload receive
	return TransferBuffer;
}

/*
 * Layout: transfer buffer(IN only), usbip_iso_packet_descriptor[].
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS urb_isoch_transfer(_In_ wsk_context &ctx, _Inout_ URB &urb)
{
	auto &ret = get_ret_submit(ctx);
	auto cnt = ret.number_of_packets;

	auto &r = urb.UrbIsochronousTransfer;
	r.ErrorCount = ret.error_count;

	if (cnt && cnt == ret.error_count) {
		r.Hdr.Status = USBD_STATUS_ISOCH_REQUEST_FAILED;
	}

	if (r.TransferFlags & USBD_START_ISO_TRANSFER_ASAP) {
		r.StartFrame = ret.start_frame;
	}

	if (cnt >= 0 && ULONG(cnt) == r.NumberOfPackets) {
		NT_ASSERT(r.NumberOfPackets == number_of_packets(ctx));
		byteswap(ctx.isoc, cnt);
	} else {
		Trace(TRACE_LEVEL_ERROR, "number_of_packets(%d) != NumberOfPackets(%lu)", cnt, r.NumberOfPackets);
		return STATUS_INVALID_PARAMETER;
	}

	char *buf{};

	if (is_transfer_dir_in(ctx.hdr)) { // TransferFlags can have wrong direction
		buf = (char*)get_transfer_buffer(r.TransferBuffer, r.TransferBufferMDL);
		if (!buf) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	}

	return fill_isoc_data(r, buf, ret.actual_length, ctx.isoc);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void log(_In_ wsk_context &ctx, _In_ const _URB_CONTROL_TRANSFER &r)
{
	auto ok = (r.TransferFlags & USBD_DEFAULT_PIPE_TRANSFER) &&
		   is_transfer_dir_in(r) &&
		   get_setup_packet(r).bRequest == USB_REQUEST_GET_DESCRIPTOR &&
		   r.TransferBufferLength >= sizeof(USB_COMMON_DESCRIPTOR);

	if (!ok) {
		return;
	}
	
	if (auto d = static_cast<USB_COMMON_DESCRIPTOR*>(ctx.mdl_buf.sysaddr())) { // MmGetSystemAddressForMdlSafe can fail
		TraceUrb("bLength %d, %!usb_descriptor_type!%!BIN!", d->bLength, d->bDescriptorType, 
			  WppBinary(d, static_cast<USHORT>(min(d->bLength, r.TransferBufferLength))));
	}
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void log_with_transfer_buffer(_In_ wsk_context &ctx, _In_ const URB &urb)
{
	switch (urb.UrbHeader.Function) {
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
	case URB_FUNCTION_CONTROL_TRANSFER: // structures are binary compatible, see urbtransfer.cpp
		static_assert(sizeof(urb.UrbControlTransfer) == sizeof(urb.UrbControlTransferEx));
		log(ctx, static_cast<const _URB_CONTROL_TRANSFER&>(urb.UrbControlTransfer));
	}
}

/* 
 * UrbHeader.Status must be set before this call.
 * @see device_ioctl.cpp, send_complete 
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void atomic_complete(_Inout_ WDFREQUEST &request, _In_ NTSTATUS status)
{
	if (auto irp = WdfRequestWdmGetIrp(request)) {
		irp->IoStatus.Status = status; // request can be completed by send_complete()
	}

	auto &req = *get_request_ctx(request);
	
	if (auto old_status = atomic_set_status(req, REQ_RECV_COMPLETE); old_status == REQ_SEND_COMPLETE) {
		complete(request, status);
	} else {
		NT_ASSERT(old_status != REQ_CANCELED);
	}

	request = WDF_NO_HANDLE;
}

enum { RECV_NEXT_USBIP_HDR = STATUS_SUCCESS, RECV_MORE_DATA_REQUIRED = STATUS_PENDING };

_Function_class_(device_ctx::received_fn)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS ret_submit(_Inout_ wsk_context &ctx)
{
	auto &ret = get_ret_submit(ctx);
	auto st = STATUS_SUCCESS;

	if (auto urb = try_get_urb(ctx.request)) { // IOCTL_INTERNAL_USB_SUBMIT_URB

		urb->UrbHeader.Status = ret.status ? to_windows_status(ret.status) : USBD_STATUS_SUCCESS;

		if (auto tr = TryAsUrbTransfer(urb)) { // see UdecxUrbRetrieveBuffer, UdecxUrbSetBytesCompleted
			if (tr->TransferBufferLength != ULONG(ret.actual_length)) { // prepare_wsk_mdl can set it
				st = assign(tr->TransferBufferLength, ret.actual_length); // DIR_OUT or !actual_length
			}
			log_with_transfer_buffer(ctx, *urb);
		}

	} else if (ret.status) {
		st = STATUS_UNSUCCESSFUL;
	}

	atomic_complete(ctx.request, st);
	return RECV_NEXT_USBIP_HDR;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto make_mdl_chain(_In_ wsk_context &ctx)
{
	MDL *head{};

	if (!ctx.is_isoc) { // IN
		head = ctx.mdl_buf.get();
		NT_ASSERT(!head->Next);
	} else if (auto &chain = ctx.mdl_buf) { // isoch IN
		auto t = tail(chain);
		t->Next = ctx.mdl_isoc.get();
		head = chain.get();
	} else { // isoch OUT or IN with zero actual_length
		head = ctx.mdl_isoc.get();
	}

	return head;
}

/*
 * If response from a server has data (actual_length > 0), URB function MUST copy it to URB
 * even if UrbHeader.Status != USBD_STATUS_SUCCESS.
 * 
 * Ensure that URB has TransferBuffer and its size is sufficient.
 * Do others checks when payload will be read.
 * 
 * recv_payload -> prepare_wsk_mdl, there is payload to receive.
 * Payload layout:
 * a) DIR_IN: any type of transfer, [transfer_buffer] OR|AND [usbip_iso_packet_descriptor...]
 * b) DIR_OUT: ISOCH, <usbip_iso_packet_descriptor...>
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto prepare_wsk_mdl(_Out_ MDL* &mdl, _Inout_ wsk_context &ctx, _Inout_ URB &urb)
{
	if (!has_transfer_buffer(urb)) {
		auto fn = urb.UrbHeader.Function;
		Trace(TRACE_LEVEL_ERROR, "%s(%#x) does not have TransferBuffer", urb_function_str(fn), fn);
		return STATUS_INVALID_PARAMETER;
	}

	auto &tr = AsUrbTransfer(urb);
	auto &ret = get_ret_submit(ctx);

	if (auto err = prepare_isoc(ctx, ret.number_of_packets)) { // sets ctx.is_isoc
		return err;
	}

	auto dir_out = is_transfer_dir_out(ctx.hdr); // TransferFlags can have wrong direction
	bool fail{};

	if (ctx.is_isoc) { // always has payload
		fail = check(tr.TransferBufferLength, ret.actual_length); // do not change buffer length
	} else { // actual_length MUST be assigned, must not have payload for OUT
		fail = assign(tr.TransferBufferLength, ret.actual_length) || dir_out; 
	}

	if (fail || !tr.TransferBufferLength) {
		Trace(TRACE_LEVEL_ERROR, "TransferBufferLength(%lu), actual_length(%d), %!usbip_dir!", 
			tr.TransferBufferLength, ret.actual_length, ctx.hdr.base.direction);
		return STATUS_INVALID_BUFFER_SIZE;
	}

	if (dir_out) {
		NT_ASSERT(ctx.is_isoc);
		NT_ASSERT(!ctx.mdl_buf);
	} else if (auto err = make_transfer_buffer_mdl(ctx.mdl_buf, ret.actual_length, ctx.is_isoc, IoWriteAccess, urb)) {
		Trace(TRACE_LEVEL_ERROR, "make_transfer_buffer_mdl %!STATUS!", err);
		return err;
	}

	mdl = make_mdl_chain(ctx);
	return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto alloc_drain_buffer(_Inout_ wsk_context &ctx, _In_ size_t length)
{  
	auto &buf = ctx.request;
	NT_ASSERT(!buf);
	return buf = (WDFREQUEST)ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, length, POOL_TAG); 
}

_Function_class_(device_ctx::received_fn)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS free_drain_buffer(_Inout_ wsk_context &ctx)
{  
	auto &buf = ctx.request;
	NT_ASSERT(buf);

	ExFreePoolWithTag(buf, POOL_TAG);
	buf = WDF_NO_HANDLE;

	return RECV_NEXT_USBIP_HDR;
};

_Function_class_(IO_COMPLETION_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS on_receive(_In_ DEVICE_OBJECT*, _In_ IRP *wsk_irp, _In_reads_opt_(_Inexpressible_("varies")) void *Context)
{
	auto &ctx = *static_cast<wsk_context*>(Context);
	auto &dev = *ctx.dev_ctx;

	auto &st = wsk_irp->IoStatus;
	TraceWSK("%!STATUS!, Information %Iu", st.Status, st.Information);

	auto err = NT_ERROR(st.Status) ? st.Status :
		   st.Information == dev.receive_size ? dev.received(ctx) :
		   st.Information ? STATUS_RECEIVE_PARTIAL : 
		   STATUS_CONNECTION_DISCONNECTED; // EOF, server called shutdown()

	switch (err) {
	case RECV_NEXT_USBIP_HDR:
		sched_receive_usbip_header(dev);
		[[fallthrough]];
	case RECV_MORE_DATA_REQUIRED:
		return StopCompletion;
	}

	if (dev.received == free_drain_buffer) { // ctx.request is a drain buffer
		free_drain_buffer(ctx);
	} else if (auto &req = ctx.request) {
		NT_ASSERT(dev.received != ret_submit); // never fails
		atomic_complete(req, STATUS_CANCELLED);
	}
	NT_ASSERT(!ctx.request);

	if (auto hdev = get_device(&dev)) {
		TraceDbg("dev %04x: unplugging after %!STATUS!", ptr04x(hdev), NT_ERROR(st.Status) ? st.Status : err);
		device::sched_plugout_and_delete(hdev);
	}

	return StopCompletion;
}

/*
 * @param received will be called if requested number of bytes are received without error
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void receive(_In_ WSK_BUF &buf, _In_ device_ctx::received_fn received, _In_ wsk_context &ctx)
{
	auto &dev = *ctx.dev_ctx;

	NT_ASSERT(verify(buf, ctx.is_isoc));
	dev.receive_size = buf.Length; // checked by verify()

	NT_ASSERT(received);
	dev.received = received;

	reuse(ctx);

	auto wsk_irp = ctx.wsk_irp; // do not access ctx or wsk_irp after send
	IoSetCompletionRoutine(wsk_irp, on_receive, &ctx, true, true, true);

	auto err = receive(dev.sock(), &buf, WSK_FLAG_WAITALL, wsk_irp);
	NT_ASSERT(err != STATUS_NOT_SUPPORTED);

	TraceWSK("%Iu bytes, %!STATUS!", buf.Length, err);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS drain_payload(_Inout_ wsk_context &ctx, _In_ size_t length)
{
	if (ULONG(length) != length) {
		Trace(TRACE_LEVEL_ERROR, "Buffer size truncation: ULONG(%lu) != size_t(%Iu)", ULONG(length), length);
		return STATUS_INVALID_PARAMETER;
	}

	if (auto addr = alloc_drain_buffer(ctx, length)) {
		ctx.mdl_buf = Mdl(addr, ULONG(length));
	} else {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", length);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (auto err = ctx.mdl_buf.prepare_nonpaged()) {
		NT_ASSERT(err != RECV_MORE_DATA_REQUIRED);
		Trace(TRACE_LEVEL_ERROR, "prepare_nonpaged %!STATUS!", err);
		free_drain_buffer(ctx);
		return err;
	}

	WSK_BUF buf{ ctx.mdl_buf.get(), 0, length };
	receive(buf, free_drain_buffer, ctx);

	return RECV_MORE_DATA_REQUIRED;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS recv_payload(_Inout_ wsk_context &ctx, _In_ size_t length)
{
	auto &urb = get_urb(ctx.request); // only IOCTL_INTERNAL_USB_SUBMIT_URB has payload
	WSK_BUF buf{ nullptr, 0, length };

	if (auto err = prepare_wsk_mdl(buf.Mdl, ctx, urb)) {
		NT_ASSERT(err != RECV_MORE_DATA_REQUIRED);
		Trace(TRACE_LEVEL_ERROR, "prepare_wsk_mdl %!STATUS!", err);
		return err;
	}

	receive(buf, ret_submit, ctx);
	return RECV_MORE_DATA_REQUIRED;
}

/*
 * For RET_UNLINK irp was completed right after CMD_UNLINK was issued.
 * @see send_cmd_unlink
 *
 * USBIP_RET_UNLINK
 * 1) if UNLINK is successful, status is -ECONNRESET
 * 2) if USBIP_CMD_UNLINK is after USBIP_RET_SUBMIT status is 0
 * See: <kernel>/Documentation/usb/usbip_protocol.rst
 */
_Function_class_(device_ctx::received_fn)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS ret_command(_Inout_ wsk_context &ctx)
{
	auto &hdr = ctx.hdr;

	ctx.request = hdr.base.command == USBIP_RET_SUBMIT ? // request must be completed
		      device::dequeue_request(*ctx.dev_ctx, hdr.base.seqnum) : WDF_NO_HANDLE;

	{
		char buf[DBG_USBIP_HDR_BUFSZ];
		TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "req %04x <- %Iu%s",
			ptr04x(ctx.request), get_total_size(hdr), dbg_usbip_hdr(buf, sizeof(buf), &hdr, false));
	}

	if (auto sz = get_payload_size(hdr)) {
		auto f = ctx.request ? recv_payload : drain_payload;
		return f(ctx, sz);
	}

	if (ctx.request) {
		ret_submit(ctx);
	}

	return RECV_NEXT_USBIP_HDR;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto validate_header(_Inout_ usbip_header &hdr)
{
	byteswap_header(hdr, swap_dir::net2host);

	auto &base = hdr.base;
	auto cmd = static_cast<usbip_request_type>(base.command);

	switch (cmd) {
	case USBIP_RET_SUBMIT: {
		auto &ret = hdr.u.ret_submit;
		if (ret.number_of_packets == number_of_packets_non_isoch) {
			ret.number_of_packets = 0;
		}
	}	break;
	case USBIP_RET_UNLINK:
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "USBIP_RET_* expected, got %!usbip_request_type!", cmd);
		return false;
	}

	auto ok = is_valid_seqnum(base.seqnum);

	if (ok) {
		base.direction = extract_dir(base.seqnum); // always zero in server response
	} else {
		Trace(TRACE_LEVEL_ERROR, "Invalid seqnum %u", base.seqnum);
	}

	return ok;
}


/*
 * A WSK application should not call new WSK functions in the context of the IoCompletion routine. 
 * Doing so may result in recursive calls and exhaust the kernel mode stack. 
 * When executing at IRQL = DISPATCH_LEVEL, this can also lead to starvation of other threads.
 *
 * For this reason work queue is used here, but reading of payload does not use it and it's OK.
 */
_Function_class_(EVT_WDF_WORKITEM)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL) // do not define as PAGED, lambda "received" must be resident
void NTAPI receive_usbip_header(_In_ WDFWORKITEM WorkItem)
{
	auto &ctx = *get_wsk_context(WorkItem);

	NT_ASSERT(!ctx.request); // must be completed and zeroed on every cycle
	ctx.mdl_buf.reset();

	ctx.mdl_hdr.next(nullptr);
	WSK_BUF buf{ ctx.mdl_hdr.get(), 0, sizeof(ctx.hdr) };

	auto received = [] (auto &ctx) // inherits PAGED from the function, can be called on DISPATCH_LEVEL
	{
		return validate_header(ctx.hdr) ? ret_command(ctx) : STATUS_INVALID_PARAMETER;
	};

	receive(buf, received, ctx);
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::complete(_In_ WDFREQUEST request, _In_ NTSTATUS status)
{
	auto irp = WdfRequestWdmGetIrp(request);

	auto info = irp->IoStatus.Information;
	NT_ASSERT(info == WdfRequestGetInformation(request));

	if (!has_urb(irp)) {
		TraceDbg("req %04x, %!STATUS!, Information %#Ix", ptr04x(request), status, info);
		WdfRequestComplete(request, status);
		return;
	}

	auto &urb = *urb_from_irp(irp);
	auto urb_st = urb.UrbHeader.Status;

	TraceDbg("req %04x, USBD_%s, %!STATUS!, Information %#Ix", ptr04x(request), get_usbd_status(urb_st), status, info);

	if (NT_SUCCESS(status)) {
		UdecxUrbComplete(request, urb_st);
	} else {
		UdecxUrbCompleteWithNtStatus(request, status);
	}
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::init_receive_usbip_header(_In_ device_ctx &ctx)
{
	PAGED_CODE();

	WDF_WORKITEM_CONFIG cfg;
	WDF_WORKITEM_CONFIG_INIT(&cfg, receive_usbip_header);
	cfg.AutomaticSerialization = false;

	WDF_OBJECT_ATTRIBUTES attrs; // WdfSynchronizationScopeNone is inherited from the driver object
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, PWSK_CONTEXT);
	attrs.EvtDestroyCallback = workitem_destroy;
	attrs.ParentObject = get_device(&ctx);

	if (auto err = WdfWorkItemCreate(&cfg, &attrs, &ctx.recv_hdr)) {
		Trace(TRACE_LEVEL_ERROR, "WdfWorkItemCreate %!STATUS!", err);
		return err;
	}

	TraceDbg("wsk workitem %04x", ptr04x(ctx.recv_hdr));

	if (auto ptr = alloc_wsk_context(&ctx, WDF_NO_HANDLE)) {
		get_wsk_context(ctx.recv_hdr) = ptr;
		return STATUS_SUCCESS;
	}

	return STATUS_INSUFFICIENT_RESOURCES;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::WskDisconnectEvent(_In_opt_ PVOID SocketContext, _In_ ULONG Flags)
{
	auto ext = static_cast<device_ctx_ext*>(SocketContext);

	if (auto dev = get_device(ext->ctx)) {
		Trace(TRACE_LEVEL_INFORMATION, "dev %04x, Flags %#lx", ptr04x(dev), Flags);
		device::sched_plugout_and_delete(dev);
	}

	return STATUS_SUCCESS;
}
