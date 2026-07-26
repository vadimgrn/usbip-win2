/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "request_list.h"
#include "trace.h"
#include "request_list.tmh"

#include "context.h"
#include "wsk_context.h"
#include "device_ioctl.h"
#include "wsk_receive.h"

namespace
{

using namespace usbip;

/*
 * @return true if the caller must enqueue device_ctx::request_completion_dpc.
 * requests_lock must be held.
 */
_IRQL_requires_same_
_IRQL_requires_(DISPATCH_LEVEL)
bool arm_completion_locked(_Inout_ device_ctx &dev, _Inout_ request_ctx &req)
{
        if (req.completion_queued) {
                return false;
        }

        req.completion_queued = true;
        InsertTailList(&dev.request_completions, &req.completion_entry);

        if (dev.request_completion_dpc_active) {
                return false;
        }

        dev.request_completion_dpc_active = true;
        return true;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void enqueue_completion_dpc_if_needed(_Inout_ device_ctx &dev, _In_ bool enqueue)
{
        if (enqueue) {
                NT_VERIFY(WdfDpcEnqueue(dev.request_completion_dpc));
        }
}

_Function_class_(EVT_WDF_DPC)
_IRQL_requires_same_
_IRQL_requires_(DISPATCH_LEVEL)
void request_completion_dpc(_In_ WDFDPC dpc)
{
        auto device = static_cast<UDECXUSBDEVICE>(WdfDpcGetParentObject(dpc));
        auto &dev = *get_device_ctx(device);

        for (;;) {
                WDFREQUEST request;
                NTSTATUS status;

                {
                        wdf::Lock lck(dev.requests_lock);

                        if (IsListEmpty(&dev.request_completions)) {
                                dev.request_completion_dpc_active = false;
                                return;
                        }

                        auto entry = RemoveHeadList(&dev.request_completions);
                        auto req = CONTAINING_RECORD(entry, request_ctx, completion_entry);

                        InitializeListHead(&req->completion_entry);

                        request = get_handle(req);
                        status = req->completion_status;
                }

                // Do not touch request or its context after this call. UDE can reuse it immediately.
                complete_now(request, status);
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto matches(_In_ WDFREQUEST request, _In_ const request_ctx &req, _In_ const device::request_search &crit)
{
        switch (crit.what) {
        case crit.SEQNUM:
                return crit.seqnum == req.seqnum;
        case crit.REQUEST:
                return crit.request == request;
        case crit.ENDPOINT:
                return crit.endpoint == req.endpoint;
        }

        Trace(TRACE_LEVEL_ERROR, "Invalid union member selector %d", crit.what);
        return false;
}

_Function_class_(EVT_WDF_REQUEST_CANCEL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void cancel_request(_In_ WDFREQUEST request)
{
        auto device = get_device(request);
        auto dev = get_device_ctx(device);

        bool removed = device::remove_request(*dev, request, false); // can clash with concurrent remove_request(, true)
        TraceDbg("%04x, removed %d", ptr04x(request), removed);

        device::send_cmd_unlink_and_cancel(device, request);
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::device::create_request_completion_dpc(
        _In_ UDECXUSBDEVICE device, _Inout_ device_ctx &dev)
{
        PAGED_CODE();

        WDF_DPC_CONFIG cfg;
        WDF_DPC_CONFIG_INIT(&cfg, request_completion_dpc);
        cfg.AutomaticSerialization = FALSE;

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = device;

        return WdfDpcCreate(&cfg, &attr, &dev.request_completion_dpc);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::ensure_request_context(_In_ WDFREQUEST request, _In_ UDECXUSBENDPOINT endpoint)
{
        NT_ASSERT(endpoint);
        auto req = get_request_ctx(request);

        if (!req) {
                WDF_OBJECT_ATTRIBUTES attr;
                WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, request_ctx);

                if (auto status = WdfObjectAllocateContext(
                        request, &attr, reinterpret_cast<void**>(&req))) {
                        Trace(TRACE_LEVEL_ERROR, "WdfObjectAllocateContext %!STATUS!", status);
                        return status;
                }
        }

        // A WDFREQUEST can be reused; its context is not zeroed between transfers.
        InitializeListHead(&req->completion_entry);
        req->endpoint = endpoint;
        req->seqnum = {};
        req->completion_status = STATUS_PENDING;
        req->completion_queued = false;

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::device::finish_request(_In_ WDFREQUEST request, _In_ NTSTATUS status)
{
        auto &req = *get_request_ctx(request);
        auto device = get_endpoint_ctx(req.endpoint)->device;
        auto &dev = *get_device_ctx(device);
        bool enqueue{};

        {
                wdf::Lock lck(dev.requests_lock);

                if (!req.completion_queued) {
                        req.completion_status = status;
                }

                enqueue = arm_completion_locked(dev, req);
        }

        enqueue_completion_dpc_if_needed(dev, enqueue);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::device::append_request(_Inout_ device_ctx &dev, _In_ const wsk_context &wsk, _In_ UDECXUSBENDPOINT endpoint)
{
        auto &req = *get_request_ctx(wsk.request); // is not zeroed
        req.cancelable = false;

        NT_ASSERT(endpoint);
        req.endpoint = endpoint;

        req.seqnum = wsk.hdr.seqnum;
        NT_ASSERT(is_valid_seqnum(req.seqnum));

        wdf::Lock lck(dev.requests_lock);
        InsertTailList(&dev.requests, &req.entry);
}

/*
 * seqnum is used instead of WDFREQUEST because
 * - request can be already completed and must be used for value comparison only
 * - if request is completed, the same request instance can be allocated from a cache
 *   for next transfer and put in the list
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::mark_request_cancelable(_Inout_ device_ctx &dev, _In_ seqnum_t seqnum)
{
        NT_ASSERT(is_valid_seqnum(seqnum));

        wdf::Lock lck(dev.requests_lock);

        for (auto head = &dev.requests, entry = head->Flink; entry != head; entry = entry->Flink) {

                if (auto req = CONTAINING_RECORD(entry, request_ctx, entry); req->seqnum != seqnum) {
                        // continue;
                } else if (auto request = get_handle(req); auto err = WdfRequestMarkCancelableEx(request, cancel_request)) {
                        TraceDbg("%04x, %!STATUS!", ptr04x(request), err);
                        RemoveEntryList(entry);
                        return err; // must do the same as cancel_request after that
                } else {
                        req->cancelable = true;
                        ++dev.cancelable_requests;
                        break;
                }
        }

        return STATUS_SUCCESS;
}

/*
 * Its rival is cancel_request if it is marked cancellable, otherwise mark_request_cancelable.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST usbip::device::remove_request(
        _Inout_ device_ctx &dev, _In_ const request_search &crit, _In_ bool unmark_cancelable)
{
        wdf::Lock lck(dev.requests_lock);

        for (auto head = &dev.requests, entry = head->Flink; entry != head; entry = entry->Flink) {

                auto req = CONTAINING_RECORD(entry, request_ctx, entry);
                auto request = get_handle(req);

                if (!matches(request, *req, crit)) {
                        continue;
                }

                RemoveEntryList(entry);

                if (!(unmark_cancelable && req->cancelable)) {
                        // not required
                } else if (auto ret = WdfRequestUnmarkCancelable(request)) {
                        TraceDbg("%04x, unmark cancelable %!STATUS!", ptr04x(request), ret);
                        if (ret != STATUS_CANCELLED) {
                                // EvtRequestCancel will not be called
                        } else if (crit.multimatch()) {
                                continue;
                        } else {
                                request = WDF_NO_HANDLE;
                        }
                }

                return request;
        }

        return WDF_NO_HANDLE;
}
