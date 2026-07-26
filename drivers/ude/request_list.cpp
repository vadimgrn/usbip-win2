/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void check_request_locked(_In_ [[maybe_unused]] const request_ctx &req)
{
        NT_ASSERT(req.endpoint);
        NT_ASSERT(!req.cancelable || req.listed);
        NT_ASSERT(!req.response_in_progress || (!req.listed && !req.cancelable));
        NT_ASSERT(!req.terminal || (!req.listed && !req.cancelable && !req.response_in_progress));
        NT_ASSERT(!req.completion_queued || (req.terminal && !req.response_in_progress));
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void remove_listed_locked(_Inout_ request_ctx &req)
{
        if (req.listed) {
                RemoveEntryList(&req.entry);
                InitializeListHead(&req.entry);
                req.listed = false;
        }
}

/*
 * @return true if the caller must enqueue device_ctx::request_completion_dpc.
 * requests_lock must be held.
 */
_IRQL_requires_same_
_IRQL_requires_(DISPATCH_LEVEL)
bool arm_completion_locked(_Inout_ device_ctx &dev, _Inout_ request_ctx &req)
{
        if (!req.terminal || req.listed || req.cancelable ||
             req.response_in_progress || req.completion_queued) {
                // a terminal request must be unlisted and not cancelable; refuse to complete otherwise
                NT_ASSERT(!req.terminal || (!req.listed && !req.cancelable));
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
                        check_request_locked(*req);

                        request = get_handle(req);
                        status = req->completion_status;
                }

                // Do not touch request or its context after this call. UDE can reuse it immediately.
                complete_now(request, status);
        }
}

_Function_class_(EVT_WDF_REQUEST_CANCEL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void cancel_request(_In_ WDFREQUEST request)
{
        auto &req = *get_request_ctx(request);
        auto endpoint = req.endpoint;
        auto device = get_endpoint_ctx(endpoint)->device;
        auto &dev = *get_device_ctx(device);

        {
                wdf::Lock lck(dev.requests_lock);
                remove_listed_locked(req);
                req.cancelable = false;
                check_request_locked(req);
        }

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
NTSTATUS usbip::device::initialize_request(
        _Inout_ device_ctx &dev, _In_ WDFREQUEST request, _In_ UDECXUSBENDPOINT endpoint)
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

        wdf::Lock lck(dev.requests_lock);

        // A WDFREQUEST can be reused; its context is not zeroed between transfers.
        NT_ASSERT(!req->listed);
        NT_ASSERT(!req->response_in_progress);

        InitializeListHead(&req->entry);
        InitializeListHead(&req->completion_entry);
        req->endpoint = endpoint;
        req->seqnum = {};
        req->completion_status = STATUS_PENDING;
        req->listed = false;
        req->cancelable = false;
        req->response_in_progress = false;
        req->terminal = false;
        req->completion_queued = false;

        check_request_locked(*req);
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::device::append_request(_Inout_ device_ctx &dev, _In_ const wsk_context &wsk)
{
        auto &req = *get_request_ctx(wsk.request);

        wdf::Lock lck(dev.requests_lock);

        NT_ASSERT(req.endpoint);
        NT_ASSERT(!req.listed);
        NT_ASSERT(!req.terminal);

        req.seqnum = wsk.hdr.seqnum;
        NT_ASSERT(is_valid_seqnum(req.seqnum));

        req.listed = true;
        InsertTailList(&dev.requests, &req.entry);
        check_request_locked(req);
}

/*
 * @return a WdfRequestMarkCancelableEx error. The caller must issue UNLINK and
 *         finish the request for that error.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::on_send_complete(
        _Inout_ device_ctx &dev,
        _In_ WDFREQUEST request,
        _In_ NTSTATUS send_status)
{
        bool enqueue{};
        NTSTATUS mark_status = STATUS_SUCCESS;

        {
                wdf::Lock lck(dev.requests_lock);
                auto &req = *get_request_ctx(request);

                if (!NT_SUCCESS(send_status)) {
                        remove_listed_locked(req);
                        // a response or cancellation that already owns the request has status precedence
                        if (!(req.terminal || req.response_in_progress)) {
                                req.completion_status = send_status;
                                req.terminal = true;
                        }
                } else if (req.terminal || req.response_in_progress) {
                        // RET_SUBMIT or cancellation won the race with WskSend completion.
                } else if (!req.listed) {
                        NT_ASSERT(false);
                        mark_status = STATUS_INVALID_DEVICE_STATE;
                } else if (auto err = WdfRequestMarkCancelableEx(request, cancel_request)) {
                        mark_status = err; // STATUS_CANCELLED if the queue is being purged
                        remove_listed_locked(req);
                } else {
                        req.cancelable = true;
                        ++dev.cancelable_requests;
                }

                enqueue = arm_completion_locked(dev, req);
                check_request_locked(req);
        }

        enqueue_completion_dpc_if_needed(dev, enqueue);
        return mark_status;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST usbip::device::begin_response(_Inout_ device_ctx &dev, _In_ seqnum_t seqnum)
{
        NT_ASSERT(is_valid_seqnum(seqnum));
        WDFREQUEST request = WDF_NO_HANDLE;

        wdf::Lock lck(dev.requests_lock);

        for (auto head = &dev.requests, entry = head->Flink; entry != head; entry = entry->Flink) {
                auto req = CONTAINING_RECORD(entry, request_ctx, entry);
                if (req->seqnum != seqnum) {
                        continue;
                }

                request = get_handle(req);
                remove_listed_locked(*req);

                if (req->cancelable) {
                        auto status = WdfRequestUnmarkCancelable(request);
                        req->cancelable = false;

                        if (status == STATUS_CANCELLED) {
                                request = WDF_NO_HANDLE;
                        } else {
                                NT_ASSERT(NT_SUCCESS(status));
                        }
                }

                if (request) {
                        req->response_in_progress = true;
                        check_request_locked(*req);
                }
                break;
        }

        return request;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::device::finish_response(_In_ WDFREQUEST request, _In_ NTSTATUS status)
{
        auto &req = *get_request_ctx(request);
        auto device = get_endpoint_ctx(req.endpoint)->device;
        auto &dev = *get_device_ctx(device);
        bool enqueue{};

        {
                wdf::Lock lck(dev.requests_lock);
                NT_ASSERT(req.response_in_progress);
                req.response_in_progress = false;

                // Preserve an earlier terminal owner; otherwise the claimed response
                // supplies the completion status.
                NT_ASSERT(!req.terminal);
                if (!req.terminal) {
                        req.completion_status = status;
                        req.terminal = true;
                }

                enqueue = arm_completion_locked(dev, req);
                check_request_locked(req);
        }

        enqueue_completion_dpc_if_needed(dev, enqueue);
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

                if (!req.terminal) {
                        req.completion_status = status;
                        req.terminal = true;
                }

                enqueue = arm_completion_locked(dev, req);
                check_request_locked(req);
        }

        enqueue_completion_dpc_if_needed(dev, enqueue);
}

_Function_class_(EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::device::cancel_queued_request(_In_ WDFQUEUE queue, _In_ WDFREQUEST request)
{
        auto endpoint = get_endpoint(queue);
        auto &dev = *get_device_ctx(get_endpoint_ctx(endpoint)->device);

        if (auto status = initialize_request(dev, request, endpoint)) {
                UdecxUrbCompleteWithNtStatus(request, status); // low-resource fallback, cannot use the DPC without request_ctx
                return;
        }
        finish_request(request, STATUS_CANCELLED);
}
