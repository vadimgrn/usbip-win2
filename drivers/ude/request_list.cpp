/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "request_list.h"
#include "trace.h"
#include "request_list.tmh"

#include "context.h"
#include "wsk_context.h"
#include "device_ioctl.h"

namespace
{

using namespace usbip;

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
        _In_ device_ctx &dev, _In_ const request_search &crit, _In_ bool unmark_cancelable)
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
