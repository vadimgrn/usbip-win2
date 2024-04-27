/*
 * Copyright (C) 2022 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbip/proto.h>
#include <libdrv/wdf_cpp.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

namespace usbip
{
        struct device_ctx;
        struct wsk_context;
}

namespace usbip::device
{

struct request_search
{
        request_search(_In_ WDFREQUEST req) : request(req), what(REQUEST) {}
        request_search(_In_ UDECXUSBENDPOINT endp) : endpoint(endp), what(ENDPOINT) {}

        request_search(_In_ seqnum_t n) : 
                request(reinterpret_cast<WDFREQUEST>(static_cast<uintptr_t>(n))), // for operator bool correctness
                what(SEQNUM) { NT_ASSERT(seqnum == n); }

        explicit operator bool() const { return request; }; // largest in union
        auto operator !() const { return !request; }

        auto multimatch() const { return what == ENDPOINT; }

        union {
                WDFREQUEST request{};
                UDECXUSBENDPOINT endpoint;
                seqnum_t seqnum;
        };

        enum what_t { SEQNUM, REQUEST, ENDPOINT };
        what_t what; // union's member selector
};


_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void append_request(_Inout_ device_ctx &dev, _In_ const wsk_context &wsk, _In_ UDECXUSBENDPOINT endpoint);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS mark_request_cancelable(_Inout_ device_ctx &dev, _In_ seqnum_t seqnum);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST remove_request(_In_ device_ctx &dev, _In_ const request_search &crit, _In_ bool unmark_cancelable = true);

} // namespace usbip::device
