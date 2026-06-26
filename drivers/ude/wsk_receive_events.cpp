/*
 * Copyright (c) 2026, Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wsk_receive_events.h"
#include <wdm.h>
#include "trace.h"
#include "wsk_receive_events.tmh"

#include "driver.h"
#include "context.h"
#include "network.h"
#include "device.h"
#include "ring_buffer.h"

#include <libdrv/wsk_cpp.h>
#include <libdrv/dbgcommon.h>
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

} // namespace


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS usbip::event::disconnect(_In_opt_ void *SocketContext, _In_ ULONG Flags)
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
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS usbip::event::receive(
        _In_opt_ void *SocketContext, _In_ ULONG Flags,
	_In_opt_ _WSK_DATA_INDICATION *DataIndication,
        _In_ SIZE_T BytesIndicated, _Inout_ SIZE_T *BytesAccepted)
{
        auto &ext = *static_cast<device_ctx_ext*>(SocketContext);
        auto &dev = *ext.ctx;
        auto device = get_handle(&dev);

	if (char buf[wsk::RECEIVE_EVENT_FLAGS_BUFBZ]; true) {
		TraceDbg("dev %04x, DataIndication %04x, BytesIndicated %Iu, Flags[%s]", ptr04x(device),
                          ptr04x(DataIndication), BytesIndicated, wsk::ReceiveEventFlags(buf, sizeof(buf), Flags));
	}

        if (!DataIndication) { // the socket must be closed ASAP
                *BytesAccepted = 0;
                return STATUS_SUCCESS;
        }

        ring_buffer cb(dev.recv_buf);

        for (auto di = DataIndication; di; di = di->Next) {

                auto &buf = di->Buffer;

                auto offs = buf.Offset; // only for the first MDL block
                auto length = buf.Length;

                for (auto mdl = buf.Mdl; mdl && length; mdl = mdl->Next, offs = 0) {

                        const ULONG priority = NormalPagePriority | MdlMappingNoExecute;

                        auto addr = (const char*)MmGetSystemAddressForMdlSafe(mdl, priority);
                        if (!addr) {
                                Trace(TRACE_LEVEL_ERROR, "MmGetSystemAddressForMdlSafe error");
                                *BytesAccepted = 0;
                                return STATUS_DATA_NOT_ACCEPTED;
                        }

                        SIZE_T len = MmGetMdlByteCount(mdl) - offs;
                        if (len > length) {
                                len = length;
                        }

                        if (auto n = cb.write(addr + offs, len)) {
                                length -= n;
                                *BytesAccepted += n;
                        }

                        if (header hdr; cb.peek_hdr(hdr)) {

                                auto data_sz = get_payload_size(hdr);

                                auto n = cb.skip(sizeof(hdr)); // do not use hdr after this
                                NT_ASSERT(n == sizeof(hdr));

                                if (data_sz && data_sz <= cb.size()) {
                                        char xxx[32];
                                        n = cb.read(xxx, data_sz);
                                        NT_ASSERT(n == data_sz);
                                }
                        }
                }
        }

        *BytesAccepted = BytesIndicated;
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::event::start_receive_data(_In_ UDECXUSBDEVICE device)
{
        PAGED_CODE();
        auto &dev = *get_device_ctx(device);

        if (auto p = realloc(dev.recv_buf, 1024)) {
                dev.recv_buf = p;
        } else {
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (auto err = wsk::event_callback_control(dev.sock(), make_event_mask(), false)) {
                Trace(TRACE_LEVEL_ERROR, "event_callback_control %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED wdm::object_reference usbip::event::stop_receive_data(_In_ UDECXUSBDEVICE device, _Inout_ bool &socket_closed)
{
        PAGED_CODE();
        auto &dev = *get_device_ctx(device);

        for (auto evt: WskEvents) {
                if (auto err = wsk::event_callback_control(dev.sock(), WSK_EVENT_DISABLE | evt, true)) {
                        Trace(TRACE_LEVEL_ERROR, "event_callback_control(%#x) %!STATUS!", evt, err);
                }
        }

        socket_closed = close_socket(dev.sock());
        return wdm::object_reference();
}
