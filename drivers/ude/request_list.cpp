/*
 * Copyright (C) 2022 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
        TraceDbg("%04x, removed %!bool!", ptr04x(request), removed);

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

        req.seqnum = wsk.hdr.base.seqnum;
        NT_ASSERT(is_valid_seqnum(req.seqnum));

        wdf::Lock lck(dev.requests_lock);
        InsertTailList(&dev.requests, &req.entry);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::mark_request_cancelable(_Inout_ device_ctx &dev, _In_ const request_search &crit)
{
        NT_ASSERT(crit);
        wdf::Lock lck(dev.requests_lock);

        for (auto head = &dev.requests, entry = head->Flink; entry != head; entry = entry->Flink) {

                auto req = CONTAINING_RECORD(entry, request_ctx, entry);
                
                if (auto request = get_handle(req); matches(request, *req, crit)) {

                        auto ret = WdfRequestMarkCancelableEx(request, cancel_request);

                        if (NT_SUCCESS(ret)) {
                                req->cancelable = true;
                        } else if (ret == STATUS_CANCELLED) { // EvtRequestCancel will not be called
                                RemoveEntryList(entry); // must do the same as cancel_request after that
                        }

                        return ret;
                }
        }

        return STATUS_NOT_FOUND;
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

                if (unmark_cancelable && req->cancelable) {

                        auto ret = WdfRequestUnmarkCancelable(request);
                        TraceWSK("%04x, unmark cancelable %!STATUS!", ptr04x(request), ret);

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
