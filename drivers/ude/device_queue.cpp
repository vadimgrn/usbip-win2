/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device_queue.h"
#include "trace.h"
#include "device_queue.tmh"

#include "context.h"
#include "device_ioctl.h"

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto matches(_In_ request_ctx *req, _In_ const device::request_search &crit)
{
        switch (crit.what) {
        case crit.REQUEST:
                return crit.request == get_handle(req);
        case crit.SEQNUM:
                return crit.seqnum == req->seqnum;
        case crit.ENDPOINT:
                return crit.endpoint == req->endpoint;
        case crit.ANY:
                return true;
        }

        Trace(TRACE_LEVEL_ERROR, !"Invalid union's member selector %d", crit.what);
        return false;
}

_Function_class_(EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI canceled_on_queue(_In_ WDFQUEUE queue, _In_ WDFREQUEST request)
{
        auto dev = get_device(queue);
        TraceDbg("dev %04x, request %04x", ptr04x(dev), ptr04x(request));

        device::send_cmd_unlink_and_cancel(dev, request);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto remove_egress_request_nolock(_Inout_ device_ctx &dev, _In_ const device::request_search &crit)
{
        WDFREQUEST handle{};
        auto head = &dev.egress_requests;

        for (auto entry = head->Flink; entry != head; entry = entry->Flink) {
                if (auto req = CONTAINING_RECORD(entry, request_ctx, entry); matches(req, crit)) {
                        RemoveEntryList(entry);
                        InitializeListHead(entry);
                        handle = get_handle(req);
                        break;
                }
        }

        return handle;
}

} // namespace


/*
 * @see device.cpp, create_endpoint_queue
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::device::create_queue(_In_ UDECXUSBDEVICE dev)
{
        PAGED_CODE();
        auto &ctx = *get_device_ctx(dev);

        WDF_IO_QUEUE_CONFIG cfg;
        WDF_IO_QUEUE_CONFIG_INIT(&cfg, WdfIoQueueDispatchManual);
        cfg.PowerManaged = WdfFalse;
        cfg.EvtIoCanceledOnQueue = canceled_on_queue;

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, UDECXUSBDEVICE);
//      attr.SynchronizationScope = WdfSynchronizationScopeQueue; // EvtIoCanceledOnQueue is used only
        attr.ParentObject = dev;

        attr.EvtCleanupCallback = [] (auto obj) 
        { 
                auto queue = static_cast<WDFQUEUE>(obj);
                TraceDbg("dev %04x, queue %04x cleanup", ptr04x(get_device(queue)), ptr04x(queue)); 
        };

        if (auto err = WdfIoQueueCreate(ctx.vhci, &cfg, &attr, &ctx.queue)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate %!STATUS!", err);
                return err;
        }

        get_device(ctx.queue) = dev;

        TraceDbg("dev %04x, queue %04x", ptr04x(dev), ptr04x(ctx.queue));
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST usbip::device::dequeue_request(_In_ device_ctx &dev, _In_ const request_search &crit)
{
        NT_ASSERT(crit.request); // largest in union

        if (!dev.queue) {
                return WDF_NO_HANDLE;
        }

        for (WDFREQUEST prev{}, cur; ; prev = cur) {

                auto st = WdfIoQueueFindRequest(dev.queue, prev, WDF_NO_HANDLE, nullptr, &cur);
                if (prev) {
                        WdfObjectDereference(prev);
                }

                switch (st) {
                case STATUS_SUCCESS:
                        if (auto req = get_request_ctx(cur); matches(req, crit)) {
                                st = WdfIoQueueRetrieveFoundRequest(dev.queue, cur, &prev);
                                WdfObjectDereference(cur);

                                switch (st) {
                                case STATUS_SUCCESS:
                                        return prev;
                                case STATUS_NOT_FOUND: // cur was canceled and removed from queue
                                        cur = WDF_NO_HANDLE; // restart the loop
                                        break;
                                default:
                                        Trace(TRACE_LEVEL_ERROR, "WdfIoQueueRetrieveFoundRequest %!STATUS!", st);
                                        return WDF_NO_HANDLE;
                                }
                        }
                        break;
                case STATUS_NOT_FOUND: // prev was canceled and removed from queue
                        NT_ASSERT(!cur); // restart the loop
                        break;
                case STATUS_NO_MORE_ENTRIES: // not found
                        return WDF_NO_HANDLE;
                default:
                        Trace(TRACE_LEVEL_ERROR, "WdfIoQueueFindRequest %!STATUS!", st);
                        return WDF_NO_HANDLE;
                }
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::device::add_egress_request(_Inout_ device_ctx &dev, _Inout_ request_ctx &req)
{
        NT_ASSERT(IsListEmpty(&req.entry));

        wdf::Lock lck(dev.egress_requests_lock);
        AppendTailList(&dev.egress_requests, &req.entry);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST usbip::device::remove_egress_request(_Inout_ device_ctx &dev, _In_ const request_search &crit)
{
        wdf::Lock lck(dev.egress_requests_lock);
        return remove_egress_request_nolock(dev, crit);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::move_egress_request_to_queue(_Inout_ device_ctx &dev, _In_ const request_search &crit)
{
        WDFREQUEST req;
        {
                wdf::Lock lck(dev.egress_requests_lock);
                req = remove_egress_request_nolock(dev, crit);
        }
        return req ? WdfRequestForwardToIoQueue(req, dev.queue) : STATUS_NOT_FOUND;
}
