/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wsk_receive_irp.h"
#include "trace.h"
#include "wsk_receive_irp.tmh"

#include "context.h"
#include "ioctl.h"
#include "device.h"
#include "driver.h"
#include "network.h"
#include "wsk_receive.h"
#include "wsk_context.h"
#include "urbtransfer.h"
#include "request_list.h"

#include <libdrv/wait_timeout.h>
#include <libdrv/usbd_helper.h>
#include <libdrv/dbgcommon.h>
#include <libdrv/pdu.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void complete_and_set_null(_Inout_ WDFREQUEST &request, _In_ NTSTATUS status)
{
        PAGED_CODE();
        device::enqueue_for_completion(request, status);
        request = WDF_NO_HANDLE;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto make_mdl_chain(_In_ wsk_context &ctx, _In_ [[maybe_unused]] bool has_buf_tail)
{
	PAGED_CODE();
	MDL *head{};

	if (!ctx.is_isoc) { // IN
		head = ctx.mdl_buf.get();
		NT_ASSERT(static_cast<bool>(head->Next) == has_buf_tail);
	} else if (auto &chain = ctx.mdl_buf) { // isoch IN
		auto t = tail(chain);
		t->Next = ctx.mdl_isoc.get();
		head = chain.get();
	} else { // isoch OUT or IN with zero actual_length
		head = ctx.mdl_isoc.get();
	}

	return head;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto make_mdl(_Inout_ Mdl &mdl, _Inout_ unique_ptr &buf, _In_ ULONG length)
{
        PAGED_CODE();

        NT_ASSERT(!mdl);
        NT_ASSERT(!buf);
        NT_ASSERT(length);

        if (buf = unique_ptr(libdrv::uninitialized, NonPagedPoolNx, length); !buf) {
                Trace(TRACE_LEVEL_ERROR, "Cannot allocate %lu bytes", length);
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (mdl = Mdl(buf.get(), length); !mdl) {
                Trace(TRACE_LEVEL_ERROR, "Cannot allocate MDL");
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        return mdl.prepare_nonpaged();
}

/*
 * If response from a server has data (actual_length > 0), URB function MUST copy it to URB
 * even if UrbHeader.Status != USBD_STATUS_SUCCESS.
 * 
 * Ensure that URB has TransferBuffer and its size is sufficient.
 * Do others checks when payload will be read.
 *
 * UdecxUrbRetrieveBuffer can return Length that less than URB.TransferBufferLength,
 * TransferBufferLength must be used instead.
 * 
 * recv_payload -> prepare_wsk_mdl, there is payload to receive.
 * Payload layout:
 * a) DIR_IN: any type of transfer, [transfer_buffer] OR|AND [usbip_iso_packet_descriptor...]
 * b) DIR_OUT: ISOCH, <usbip_iso_packet_descriptor...>
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto prepare_wsk_mdl(_Inout_ MDL* &mdl, _Inout_ wsk_context &ctx)
{
	PAGED_CODE();
        NT_ASSERT(!mdl);

        auto &ret = get_ret_submit(ctx.hdr);

        auto st = prepare_isoc(ctx, ret.number_of_packets); // sets ctx.is_isoc
        if (NT_ERROR(st)) {
                return st;
        }

        auto &urb = get_urb(ctx.request); // only IOCTL_INTERNAL_USB_SUBMIT_URB has payload

        ULONG TransferBufferLength{};
        UCHAR *buf;
        st = UdecxUrbRetrieveBuffer(ctx.request, &buf, &TransferBufferLength); // URB must have transfer buffer

        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUrbRetrieveBuffer(%s) %!STATUS!", urb_function_str(urb.UrbHeader.Function), st);
                return st;
        }
        TransferBufferLength = AsUrbTransfer(urb).TransferBufferLength; // ignore Length from UdecxUrbRetrieveBuffer

        auto dir_out = is_transfer_dir_out(ctx.hdr);
	bool fail{};

	if (ctx.is_isoc) { // always has payload
		fail = NT_ERROR(check(TransferBufferLength, ret.actual_length)); // do not change buffer length
	} else { // actual_length MUST be assigned, must not have payload for OUT
		fail = NT_ERROR(assign(TransferBufferLength, ret.actual_length)) || dir_out;
		UdecxUrbSetBytesCompleted(ctx.request, TransferBufferLength);
	}

	if (fail || !TransferBufferLength) {
		Trace(TRACE_LEVEL_ERROR, "TransferBufferLength(%lu), actual_length(%d), %!usbip_dir!", 
			                  TransferBufferLength, ret.actual_length, ctx.hdr.direction);

                return STATUS_INVALID_BUFFER_SIZE;
	}

        bool has_tail{};

	if (dir_out) {
		NT_ASSERT(ctx.is_isoc);
		NT_ASSERT(!ctx.mdl_buf);
	} else {
                st = make_transfer_buffer_mdl(ctx.mdl_buf, ret.actual_length, IoWriteAccess, urb);
                if (NT_ERROR(st)) {
		        Trace(TRACE_LEVEL_ERROR, "make_transfer_buffer_mdl %!STATUS!", st);
		        return st;
                }

                if (auto len = size(ctx.mdl_buf); len < ret.actual_length) {

                        auto gap = static_cast<ULONG>(ret.actual_length - len);

                        st = alloc_mdl_buf_tail(ctx, gap);
                        if (NT_ERROR(st)) {
                                return st;
                        }

                        NT_ASSERT(!ctx.mdl_buf.next());
                        ctx.mdl_buf.next(ctx.mdl_buf_tail);
                        has_tail = true;
                }
        }

	mdl = make_mdl_chain(ctx, has_tail);
	return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto receive(_Inout_ wsk_context &ctx, _Inout_ WSK_BUF &buf)
{
	PAGED_CODE();

	auto &dev = *ctx.dev;
	NT_ASSERT(verify(buf, ctx.is_isoc));

	SIZE_T actual{};
	auto st = receive(dev.sock(), &buf, WSK_FLAG_WAITALL, &actual);

	TraceWSK("req %04x, %!STATUS!, %Iu byte(s)", ptr04x(ctx.request), st, actual);

	return  NT_ERROR(st) ? st :
		actual == buf.Length ? STATUS_SUCCESS :
		actual ? STATUS_RECEIVE_PARTIAL : 
		STATUS_CONNECTION_DISCONNECTED; // EOF
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto drain_payload(_Inout_ wsk_context &ctx, _In_ size_t length)
{
	PAGED_CODE();

	if (auto len = static_cast<ULONG>(length); len != length) {
		Trace(TRACE_LEVEL_ERROR, "Buffer size truncation: ULONG(%lu) != size_t(%Iu)", len, length);
		return STATUS_INVALID_PARAMETER;
	}

        unique_ptr payload;
        auto st = make_mdl(ctx.mdl_buf, payload, static_cast<ULONG>(length));
        if (NT_ERROR(st)) {
                return st;
        }

	WSK_BUF buf{ .Mdl = ctx.mdl_buf.get(), .Length = length };
	return receive(ctx, buf);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto recv_payload(_Inout_ wsk_context &ctx, _In_ size_t length)
{
	PAGED_CODE();
        WSK_BUF buf{ .Length = length };

        auto st = prepare_wsk_mdl(buf.Mdl, ctx);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "prepare_wsk_mdl %!STATUS!", st);
                return st;
        }

        return receive(ctx, buf);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto recv_usbip_header(_Inout_ wsk_context &ctx)
{
	PAGED_CODE();

	ctx.mdl_buf.reset();
	ctx.mdl_hdr.next(nullptr);

	WSK_BUF buf{ .Mdl = ctx.mdl_hdr.get(), .Length = sizeof(ctx.hdr) };

        auto st = receive(ctx, buf);
	if (NT_ERROR(st)) {
		return st;
	}

	return validate(ctx.hdr) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void recv_loop(_Inout_ device_ctx &dev, _Inout_ wsk_context &ctx)
{
	PAGED_CODE();

	for (NTSTATUS status{}; NT_SUCCESS(status); ) {

                if (get_flag(dev.unplugged) || NT_ERROR(recv_usbip_header(ctx))) [[unlikely]] {
                        break;
                }

		NT_ASSERT(!ctx.request); // must be completed and zeroed on every loop
		ctx.request = ret_command(ctx.hdr, dev);

                if (size_t sz; !get_payload_size(sz, ctx.hdr)) [[unlikely]] {
                        Trace(TRACE_LEVEL_ERROR, "invalid PDU");
                        status = STATUS_INVALID_PARAMETER;
                } else if (!sz) {
                        // no payload
                } else if (get_flag(dev.unplugged)) [[unlikely]] {
                        status = STATUS_CANCELLED;
                } else {
                        auto f = ctx.request ? recv_payload : drain_payload;
                        status = f(ctx, sz);
                }

		if (auto &req = ctx.request) {
			auto st = NT_ERROR(status) ? status : ret_submit(ctx);
			complete_and_set_null(req, st);
		}
	}
}

_IRQL_requires_same_
_Function_class_(KSTART_ROUTINE)
PAGED void recv_thread_function(_In_ void *context)
{
	PAGED_CODE();

	auto device = static_cast<UDECXUSBDEVICE>(context);
	TraceDbg("dev %04x", ptr04x(device));

	auto &dev = *get_device_ctx(device);

	if (auto ctx = alloc_wsk_context(&dev, WDF_NO_HANDLE)) {
		recv_loop(dev, *ctx);
		NT_ASSERT(!ctx->request);
		free(ctx, true);
	}

	if (!get_flag(dev.unplugged)) {
		TraceDbg("dev %04x, detaching", ptr04x(device));
		device::async_detach_and_delete(device, true); // detach will be called by this thread
	}

	TraceDbg("dev %04x, exited", ptr04x(device));
}

} // namespace


/*
 * ObDereferenceObject must be called as soon as it is done with this thread
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::start_receive_data_irp(_In_ UDECXUSBDEVICE device)
{
        PAGED_CODE();
        const auto access = THREAD_ALL_ACCESS;

        HANDLE handle{};
        auto st = PsCreateSystemThread(&handle, access, nullptr, nullptr, nullptr, recv_thread_function, device);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "PsCreateSystemThread %!STATUS!", st);
                return st;
        }

        auto dev = get_device_ctx(device);

        PVOID thread{};
        NT_VERIFY(NT_SUCCESS(ObReferenceObjectByHandle(handle, access, *PsThreadType, KernelMode, &thread, nullptr)));

        NT_VERIFY(!InterlockedExchangePointer(reinterpret_cast<PVOID*>(&dev->recv_thread), thread));
        NT_VERIFY(NT_SUCCESS(ZwClose(handle)));

        TraceDbg("dev %04x", ptr04x(device));
        return STATUS_SUCCESS;
}

/*
 * @return object_reference defer dereference of receive thread if it executes this function
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED wdm::object_reference usbip::stop_receive_data_irp(_In_ UDECXUSBDEVICE device, _Inout_ bool &socket_closed)
{
        PAGED_CODE();

        auto &dev = *get_device_ctx(device);
        socket_closed = close_socket(dev.sock());

        wdm::object_reference thread(InterlockedExchangePointer(reinterpret_cast<PVOID*>(&dev.recv_thread), nullptr), false);

        if (!thread) {
                TraceDbg("dev %04x, was not created", ptr04x(device));
                return thread;
        } else if (thread.get() == KeGetCurrentThread()) { // called by receive thread
                thread.set_defer_delete();
                return thread;
        }

        NT_ASSERT(get_flag(dev.unplugged)); // thread checks it
        TraceDbg("dev %04x", ptr04x(device));

        auto timeout = make_timeout(1*wdm::minute, wdm::period::relative);
        auto st = KeWaitForSingleObject(thread.get(), Executive, KernelMode, false, &timeout);

        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "dev %04x, KeWaitForSingleObject %!STATUS!", ptr04x(device), st);
        } else {
                TraceDbg("dev %04x, joined", ptr04x(device));
        }

        thread.reset(); // it's safe to dereference(delete) now
        return thread;
}
