/*
 * Copyright (c) 2026, Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wsk_receive_events.h"
#include "trace.h"
#include "wsk_receive_events.tmh"

#include "context.h"
#include "network.h"
#include "device.h"
#include "ring_buffer.h"
#include "wsk_context.h"
#include "wsk_receive.h"
#include "request_list.h"

#include <libdrv/pdu.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto received(_In_ UDECXUSBDEVICE device, _Inout_ device_ctx &dev, _In_ const char *data, _In_ size_t len)
{
        NT_ASSERT(len);

        wsk_context ctx{ .dev = &dev };
        auto &hdr = ctx.hdr;

        ring_buffer rb(dev.recv_buf);

        for (auto has_hdr = rb.peek_hdr(hdr); len || has_hdr; has_hdr = rb.peek_hdr(hdr)) {

                if (auto n = rb.write(data, len)) {
                        data += n;
                        len -= n;
                        if (!has_hdr) {
                                has_hdr = rb.peek_hdr(hdr);
                        }
                }

                if (!has_hdr) {
                        break;
                }

                if (!validate(hdr)) [[unlikely]] {
                        return false;
                }

                size_t expected;
                if (!get_total_size(expected, hdr)) [[unlikely]] {
                        Trace(TRACE_LEVEL_ERROR, "invalid PDU");
                        return false;
                }

                if (rb.capacity() >= expected) [[likely]] {
                        //
                } else {
                        auto st = realloc(dev.recv_buf, expected);
                        if (NT_ERROR(st)) {
                                return false;
                        }
                        rb = ring_buffer(dev.recv_buf);
                        TraceDbg("dev %04x, ring buffer capacity %Iu", ptr04x(device), rb.capacity());
                }

                if (rb.size() < expected) {
                        break;
                }

                if (ctx.request = ret_command(hdr, dev); !ctx.request) {
                        rb.skip(expected);
                        continue;
                }

                expected = rb.size() - expected;
                rb.skip(sizeof(hdr));

                auto st = ret_submit(ctx); // must consume payload
                device::enqueue_for_completion(ctx.request, st);

                if (rb.size() != expected) [[unlikely]] {
                        Trace(TRACE_LEVEL_ERROR, "dev %04x, ring buffer size %Iu != %Iu", 
                                                  ptr04x(device), rb.size(), expected);
                        return false;
                }
        }

        return !len;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto async_reattach(_In_ UDECXUSBDEVICE device, _Inout_ device_ctx &dev, _In_ NTSTATUS st)
{
        if (get_flag(dev.unplugged)) {
                TraceDbg("dev %04x, %!STATUS!", ptr04x(device), st);
        } else {
                TraceDbg("dev %04x, detaching, %!STATUS!", ptr04x(device), st);
                device::async_detach_and_delete(device, true);
        }

        return st;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
auto wsk_receive(
        _In_opt_ void *SocketContext, _In_ ULONG Flags,
	_In_opt_ _WSK_DATA_INDICATION *DataIndication,
        _In_ SIZE_T BytesIndicated, _Inout_ SIZE_T *BytesAccepted)
{
        *BytesAccepted = 0;

        auto ext = static_cast<device_ctx_ext*>(SocketContext);
        auto &dev = *ext->ctx;
        auto device = get_handle(&dev);

        if (WPP_LEVEL_FLAGS_ENABLED(TRACE_LEVEL_VERBOSE, FLAG_WSK)) {
                char buf[wsk::RECEIVE_EVENT_FLAGS_BUFBZ];
                TraceWSK("dev %04x, BytesIndicated %Iu, Flags[%s]", ptr04x(device),
                          BytesIndicated, wsk::ReceiveEventFlags(buf, sizeof(buf), Flags));
        }

        if (!DataIndication) [[unlikely]] { // the socket must be closed ASAP, WSK_EVENT_DISCONNECT will not be called
                return async_reattach(device, dev, STATUS_SUCCESS);
        }

        for (auto di = DataIndication; di; di = di->Next) {

                auto &buf = di->Buffer;

                auto offset = buf.Offset; // only for the first MDL block
                auto length = buf.Length;

                for (auto mdl = buf.Mdl; mdl && length; mdl = mdl->Next, offset = 0) {

                        const ULONG priority = NormalPagePriority | MdlMappingNoExecute;

                        auto addr = (const char*)MmGetSystemAddressForMdlSafe(mdl, priority);
                        if (!addr) [[unlikely]] {
                                Trace(TRACE_LEVEL_ERROR, "MmGetSystemAddressForMdlSafe error");
                                return async_reattach(device, dev, STATUS_DATA_NOT_ACCEPTED);
                        }

                        SIZE_T len = MmGetMdlByteCount(mdl) - offset;
                        len = min(len, length);

                        if (!received(device, dev, addr + offset, len)) [[unlikely]] {
                                return async_reattach(device, dev, STATUS_DATA_NOT_ACCEPTED);
                        }

                        *BytesAccepted += len;
                        length -= len;
                }
        }

        NT_ASSERT(*BytesAccepted == BytesIndicated);
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
auto wsk_disconnect(_In_opt_ void *SocketContext, _In_ ULONG Flags)
{
        auto ext = static_cast<device_ctx_ext*>(SocketContext);
        auto &dev = *ext->ctx;
        auto device = get_handle(&dev);

        if (char buf[wsk::DISCONNECT_EVENT_FLAGS_BUFBZ]; true) {
                TraceDbg("dev %04x, Flags[%s]", ptr04x(device), wsk::DisconnectEventFlags(buf, sizeof(buf), Flags));
        }

        return async_reattach(device, dev, STATUS_SUCCESS);
}

const ULONG wsk_events[] {WSK_EVENT_RECEIVE, WSK_EVENT_DISCONNECT};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto make_event_mask()
{
        ULONG mask = 0;
        for (auto evt: wsk_events) {
                mask |= evt;
        }
        return mask;
}

const WSK_CLIENT_CONNECTION_DISPATCH g_dispatch
{
        .WskReceiveEvent = wsk_receive,
        .WskDisconnectEvent = wsk_disconnect
};

} // namespace


const void *usbip::events::wsk_dispatch = &g_dispatch;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::events::start_receive_data(_In_ UDECXUSBDEVICE device)
{
        PAGED_CODE();
        auto &dev = *get_device_ctx(device);

        auto st = realloc(dev.recv_buf, 1); // single page, for the beginning
        if (NT_ERROR(st)) {
                return st;
        }

        st = wsk::event_callback_control(dev.sock(), make_event_mask(), false);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "event_callback_control %!STATUS!", st);
        }
        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED wdm::object_reference usbip::events::stop_receive_data(_In_ UDECXUSBDEVICE device, _Inout_ bool &socket_closed)
{
        PAGED_CODE();
        auto &dev = *get_device_ctx(device);

        for (auto evt: wsk_events) {
                auto st = wsk::event_callback_control(dev.sock(), WSK_EVENT_DISABLE | evt, true);
                if (NT_ERROR(st)) {
                        Trace(TRACE_LEVEL_ERROR, "event_callback_control(%#x) %!STATUS!", evt, st);
                }
        }

        socket_closed = close_socket(dev.sock());

        if (auto &buf = dev.recv_buf) {
                TraceDbg("ring buffer capacity %Iu", buf->capacity);
                free(buf);
        }

        return wdm::object_reference();
}
