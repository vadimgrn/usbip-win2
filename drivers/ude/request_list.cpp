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

#include <libdrv/ioctl.h>
#include <libdrv/lists.h>
#include <libdrv/dbgcommon.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto in_sent_list(_In_ const request_ctx &req)
{
        return !libdrv::is_zeroed(req.entry);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto in_completion_queue(_In_ const request_ctx &req)
{
        return !libdrv::is_zeroed(req.completion_entry);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void check_request_locked(_In_ [[maybe_unused]] const request_ctx &req)
{
        NT_ASSERT(req.endpoint);

//      a request can only be marked cancelable after WskSend has completed and while still waiting in the sent list
        NT_ASSERT(!req.cancelable || (in_sent_list(req) && !req.send_completion_pending));

//      once a response PDU claims the request, it is removed from the sent list and unmarked cancelable
        NT_ASSERT(!req.response_in_progress || (!in_sent_list(req) && !req.cancelable));

//      terminal state is mutually exclusive with active list membership or in-flight I/O
        NT_ASSERT(!req.terminal || (!in_sent_list(req) && !req.cancelable && !req.response_in_progress));

//      if a request is currently in the completion queue, it must be terminal and have no asynchronous I/O in flight
        NT_ASSERT(!in_completion_queue(req) ||
                  (req.terminal && !req.send_completion_pending && !req.response_in_progress));
}

/*
 * requests_lock must be held.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void remove_from_sent_list_locked(_Inout_ request_ctx &req)
{
        if (in_sent_list(req)) {
                RemoveEntryList(&req.entry);
                req.entry = LIST_ENTRY{};
        }
}

/*
 * requests_lock must be held.
 * @return true if the caller must enqueue device_ctx::request_completion_dpc.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto add_to_completion_queue_locked(_Inout_ device_ctx &dev, _Inout_ request_ctx &req)
{
        if (!req.terminal || req.cancelable || req.send_completion_pending ||
             req.response_in_progress || in_sent_list(req) || in_completion_queue(req)) {
                // a terminal request must be unlisted and not cancelable; refuse to complete otherwise
                NT_ASSERT(!req.terminal || (!in_sent_list(req) && !req.cancelable));
                return false;
        }

        bool was_empty = IsListEmpty(&dev.request_completions);
        InsertTailList(&dev.request_completions, &req.completion_entry);
        return was_empty;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void complete(_In_ WDFREQUEST request, _In_ NTSTATUS status)
{
	auto irp = WdfRequestWdmGetIrp(request);

	auto info = irp->IoStatus.Information;
	NT_ASSERT(info == WdfRequestGetInformation(request));

	auto &req = *get_request_ctx(request);

	if (!libdrv::has_urb(irp)) {
		if (NT_ERROR(status)) {
			TraceUrb("seqnum %u, %!STATUS!, Information %#Ix", req.seqnum, status, info);
		}
		WdfRequestComplete(request, status);
		return;
	}

	auto &urb = *libdrv::urb_from_irp(irp);
	auto &urb_st = urb.UrbHeader.Status;

	if (status == STATUS_CANCELLED && urb_st == USBD_STATUS_PENDING) {
		urb_st = USBD_STATUS_CANCELED; // FIXME: is this really required?
	}

	if (NT_ERROR(status) || urb_st) {
		TraceUrb("seqnum %u, USBD_%s, %!STATUS!, Information %#Ix", 
			  req.seqnum, get_usbd_status(urb_st), status, info);
	}

	if (NT_SUCCESS(status)) {
		UdecxUrbComplete(request, urb_st);
	} else {
		UdecxUrbCompleteWithNtStatus(request, status);
	}
}

_Function_class_(EVT_WDF_DPC)
_IRQL_requires_same_
_IRQL_requires_(DISPATCH_LEVEL)
void request_completion_dpc(_In_ WDFDPC dpc)
{
        auto device = static_cast<UDECXUSBDEVICE>(WdfDpcGetParentObject(dpc));
        auto &dev = *get_device_ctx(device);

        for (auto head = &dev.request_completions; ; ) {

                WDFREQUEST request;
                NTSTATUS status;

                {
                        wdf::Lock lck(dev.requests_lock);

                        if (IsListEmpty(head)) {
                                break;
                        }

                        auto entry = RemoveHeadList(head);
                        *entry = LIST_ENTRY{};

                        auto &req = *CONTAINING_RECORD(entry, request_ctx, completion_entry);
                        NT_ASSERT(!in_sent_list(req));
                        check_request_locked(req);

                        request = get_handle(&req);
                        status = req.completion_status;
                }

                complete(request, status); // do not touch request or its context after this call
        }
}

_Function_class_(EVT_WDF_REQUEST_CANCEL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void cancel(_In_ WDFREQUEST request)
{
        auto &req = *get_request_ctx(request);
        auto device = get_endpoint_ctx(req.endpoint)->device;
        auto dev = get_device_ctx(device);

        {
                wdf::Lock lck(dev->requests_lock);

                remove_from_sent_list_locked(req);
                if (req.cancelable) {
                        req.cancelable = false;
                        --dev->cancelable_requests;
                }
                check_request_locked(req);
        }

        device::send_cmd_unlink_and_cancel(device, request);
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::device::create_request_completion_dpc(_In_ UDECXUSBDEVICE device, _Inout_ device_ctx &dev)
{
        PAGED_CODE();

        WDF_DPC_CONFIG cfg;
        WDF_DPC_CONFIG_INIT(&cfg, request_completion_dpc);
        cfg.AutomaticSerialization = false;

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = device;

        auto st = WdfDpcCreate(&cfg, &attr, &dev.request_completion_dpc);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfDpcCreate %!STATUS!", st);
        }
        return st;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::init_request_ctx(_In_ WDFREQUEST request, _In_ UDECXUSBENDPOINT endpoint)
{
        if (!(request && endpoint)) {
                Trace(TRACE_LEVEL_ERROR, "request %04x, endpoint %04x", ptr04x(request), ptr04x(endpoint));
                return STATUS_INVALID_PARAMETER;
        }

        auto req = get_request_ctx(request);
        if (!req) {
                WDF_OBJECT_ATTRIBUTES attr;
                WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, request_ctx);

                auto st = WdfObjectAllocateContext(request, &attr, reinterpret_cast<PVOID*>(&req));
                if (NT_ERROR(st)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfObjectAllocateContext %!STATUS!", st);
                        return st;
                }
        }

        // request can be reused, its context is not zeroed between transfers

        NT_ASSERT(!in_sent_list(*req));
        NT_ASSERT(!in_completion_queue(*req));
        NT_ASSERT(!req->send_completion_pending);
        NT_ASSERT(!req->response_in_progress);

        *req = request_ctx {
                .endpoint = endpoint,
                .completion_status = STATUS_PENDING
        };

        check_request_locked(*req);
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::device::add_request_to_sent_list(_Inout_ device_ctx &dev, _In_ WDFREQUEST request, _In_ seqnum_t seqnum)
{
        auto &req = *get_request_ctx(request);

        NT_ASSERT(!in_sent_list(req));
        NT_ASSERT(!in_completion_queue(req));
        NT_ASSERT(req.endpoint);
        NT_ASSERT(!req.terminal);

        NT_ASSERT(is_valid_seqnum(seqnum));
        req.seqnum = seqnum;

        NT_ASSERT(!req.send_completion_pending);
        req.send_completion_pending = true;

        check_request_locked(req);

        {
                wdf::Lock lck(dev.requests_lock);
                InsertTailList(&dev.requests, &req.entry);
        }
}

/*
 * The WDFREQUEST cannot be completed until this function clears send_completion_pending.
 * That keeps the upper URB TransferBufferMDL valid for the entire WskSend.
 *
 * @return a WdfRequestMarkCancelableEx error. The caller must issue UNLINK and
 *         finish the request for that error.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::on_send_complete(
        _Inout_ device_ctx &dev,
        _In_ WDFREQUEST request,
        _In_ seqnum_t seqnum,
        _In_ NTSTATUS send_status)
{
        auto &req = *get_request_ctx(request);

        auto mark_status = STATUS_SUCCESS;
        bool enqueue{};

        {
                wdf::Lock lck(dev.requests_lock);

                NT_ASSERT(req.seqnum == seqnum);
                NT_ASSERT(req.send_completion_pending);

                if (req.seqnum != seqnum || !req.send_completion_pending) {
                        return STATUS_INVALID_DEVICE_STATE;
                }

                req.send_completion_pending = false;

                if (NT_ERROR(send_status)) {
                        remove_from_sent_list_locked(req);
                        // a response or cancellation that already owns the request has status precedence
                        if (!(req.terminal || req.response_in_progress)) {
                                req.completion_status = send_status;
                                req.terminal = true;
                        }
                } else if (req.terminal || req.response_in_progress) {
                        // RET_SUBMIT or cancellation won the race with WskSend completion
                } else if (!in_sent_list(req)) {
                        NT_ASSERT(!"!in_sent_list");
                        mark_status = STATUS_INVALID_DEVICE_STATE;
                } else {
                        auto st = WdfRequestMarkCancelableEx(request, cancel);
                        if (NT_ERROR(st)) {
                                mark_status = st; // STATUS_CANCELLED if the queue is being purged
                                remove_from_sent_list_locked(req);
                        } else {
                                req.cancelable = true;
                                ++dev.cancelable_requests;
                        }
                }

                enqueue = add_to_completion_queue_locked(dev, req);
                check_request_locked(req);
        }

        if (enqueue) {
                NT_VERIFY(WdfDpcEnqueue(dev.request_completion_dpc));
        }

        return mark_status;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST usbip::device::find_sent_request(_Inout_ device_ctx &dev, _In_ seqnum_t seqnum)
{
        NT_ASSERT(is_valid_seqnum(seqnum));
        WDFREQUEST request{};

        wdf::Lock lck(dev.requests_lock);

        for (auto head = &dev.requests, entry = head->Flink; entry != head; entry = entry->Flink) {

                auto &req = *CONTAINING_RECORD(entry, request_ctx, entry);
                if (req.seqnum != seqnum) {
                        continue;
                }

                RemoveEntryList(entry);
                *entry = LIST_ENTRY{};

                NT_ASSERT(!in_completion_queue(req));
                request = get_handle(&req);

                if (req.cancelable) {
                        auto status = WdfRequestUnmarkCancelable(request);
                        req.cancelable = false;
                        --dev.cancelable_requests;

                        if (status == STATUS_CANCELLED) {
                                request = WDF_NO_HANDLE;
                        } else {
                                NT_ASSERT(NT_SUCCESS(status));
                        }
                }

                if (request) {
                        NT_ASSERT(!req.response_in_progress);
                        req.response_in_progress = true;
                        check_request_locked(req);
                }

                break;
        }

        return request;
}

/*
 * Publish a terminal request/reqponse state. The device completion DPC performs
 * the actual UDE completion after both response processing and WskSend are done.
 * @see Write a UDE client driver
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::device::enqueue_for_completion(_In_ WDFREQUEST request, _In_ NTSTATUS status)
{
        auto &req = *get_request_ctx(request);
        auto device = get_endpoint_ctx(req.endpoint)->device;
        auto &dev = *get_device_ctx(device);

        bool enqueue{};

        {
                wdf::Lock lck(dev.requests_lock);

                if (req.response_in_progress) {
                        req.response_in_progress = false;
                        NT_ASSERT(!req.terminal);
                }

                if (!req.terminal) { // preserve an earlier terminal owner
                        req.completion_status = status; // the claimed response supplies the completion status
                        req.terminal = true;
                }

                enqueue = add_to_completion_queue_locked(dev, req);
                check_request_locked(req);
        }

        if (enqueue) {
                NT_VERIFY(WdfDpcEnqueue(dev.request_completion_dpc));
        }
}
