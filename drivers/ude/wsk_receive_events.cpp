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

#include <libdrv/pdu.h>

namespace
{

using namespace usbip;
const ULONG WskEvents[] {WSK_EVENT_RECEIVE, WSK_EVENT_DISCONNECT};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto make_event_mask()
{
        ULONG mask = 0;
        for (auto evt: WskEvents) {
                mask |= evt;
        }
        return mask;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto received(_Inout_ device_ctx &dev, _In_ const char *data, _In_ size_t len)
{
        NT_ASSERT(len);

        wsk_context ctx{ .dev = &dev };
        auto &hdr = ctx.hdr;

        ring_buffer rb(dev.recv_buf);

        for (auto has_hdr = rb.peek_hdr(hdr); len || has_hdr; has_hdr = rb.peek_hdr(hdr)) {

                if (!len) {
                        // has_hdr
                } else if (auto n = rb.write(data, len)) {
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

                auto expected = get_total_size(hdr);

                if (auto cap = rb.capacity(); cap < expected) [[unlikely]] {
                        if (auto err = rb.realloc(expected)) {
                                return false;
                        } else {
                                TraceDbg("ring buffer capacity %Iu -> %Iu", cap, rb.capacity());
                        }
                }

                if (rb.size() < expected) {
                        break;
                }

                if (ctx.request = ret_command(hdr, dev); ctx.request) {
                        expected -= rb.skip(sizeof(hdr));
                        auto st = ret_submit(ctx);
                        if (st == STATUS_DATA_NOT_ACCEPTED) [[unlikely]] {
                                complete(ctx.request, STATUS_UNSUCCESSFUL);
                                return false;
                        }
                        complete(ctx.request, st);
                }

                rb.skip(expected);
        }

        return !len;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto stop_receive(_In_ UDECXUSBDEVICE device, _Inout_ device_ctx &dev, _In_ NTSTATUS st)
{
        if (get_flag(dev.unplugged)) {
                TraceDbg("dev %04x, %!STATUS!", ptr04x(device), st);
        } else {
                TraceDbg("dev %04x, detaching, %!STATUS!", ptr04x(device), st);
                device::async_detach_and_delete(device, NT_ERROR(st));
        }

        return st;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS usbip::events::receive(
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

        if (!DataIndication) [[unlikely]] { // the socket must be closed ASAP
                return stop_receive(device, dev, STATUS_SUCCESS);
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
                                return stop_receive(device, dev, STATUS_DATA_NOT_ACCEPTED);
                        }

                        SIZE_T len = MmGetMdlByteCount(mdl) - offset;
                        len = min(len, length);

                        if (!received(dev, addr + offset, len)) [[unlikely]] {
                                return stop_receive(device, dev, STATUS_DATA_NOT_ACCEPTED);
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
NTSTATUS usbip::events::disconnect(_In_opt_ void *SocketContext, _In_ ULONG Flags)
{
        auto ext = static_cast<device_ctx_ext*>(SocketContext);
        auto device = get_handle(ext->ctx);

        if (char buf[wsk::DISCONNECT_EVENT_FLAGS_BUFBZ]; true) {
                TraceDbg("dev %04x, Flags[%s]", ptr04x(device), wsk::DisconnectEventFlags(buf, sizeof(buf), Flags));
        }

        device::async_detach_and_delete(device);
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::events::start_receive_data(_In_ UDECXUSBDEVICE device)
{
        PAGED_CODE();
        auto &dev = *get_device_ctx(device);

        if (auto err = realloc(dev.recv_buf, 1)) { // single page, for the beginning
                return err;
        }

        if (auto err = wsk::event_callback_control(dev.sock(), make_event_mask(), false)) {
                Trace(TRACE_LEVEL_ERROR, "event_callback_control %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED wdm::object_reference usbip::events::stop_receive_data(_In_ UDECXUSBDEVICE device, _Inout_ bool &socket_closed)
{
        PAGED_CODE();
        auto &dev = *get_device_ctx(device);

        for (auto evt: WskEvents) {
                if (auto err = wsk::event_callback_control(dev.sock(), WSK_EVENT_DISABLE | evt, true)) {
                        Trace(TRACE_LEVEL_ERROR, "event_callback_control(%#x) %!STATUS!", evt, err);
                }
        }

        socket_closed = close_socket(dev.sock());

        if (auto &buf = dev.recv_buf) {
                TraceDbg("ring buffer capacity %Iu", buf->capacity);
                free(buf);
        }

        return wdm::object_reference();
}
