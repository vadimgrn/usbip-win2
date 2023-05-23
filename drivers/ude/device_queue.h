/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbip\proto.h>

#include <libdrv\codeseg.h>
#include <libdrv\wdf_cpp.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

namespace usbip
{
        struct device_ctx;
        struct request_ctx;
}

namespace usbip::device
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create_queue(_In_ UDECXUSBDEVICE dev);

struct request_search
{
        request_search() = default;
        request_search(_In_ WDFREQUEST req) : request(req), what(REQUEST) {}
        request_search(_In_ UDECXUSBENDPOINT endp) : endpoint(endp), what(ENDPOINT) {}

        request_search(_In_ seqnum_t n) : 
                endpoint(reinterpret_cast<UDECXUSBENDPOINT>(static_cast<uintptr_t>(n))),
                what(SEQNUM) {}

        union {
                WDFREQUEST request{};
                UDECXUSBENDPOINT endpoint;
                seqnum_t seqnum;
                static_assert(sizeof(request) >= sizeof(seqnum));
        };

        enum what_t { ANY, REQUEST, ENDPOINT, SEQNUM };
        what_t what = ANY; // union's member selector
};

/*
 * @param queue device_ctx.queue 
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST dequeue_request(_In_ device_ctx &dev, _In_ const request_search &crit);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void add_egress_request(_Inout_ device_ctx &dev, _Inout_ request_ctx &req);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST remove_egress_request(_Inout_ device_ctx &dev, _In_ const request_search &crit);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS move_egress_request_to_queue(_Inout_ device_ctx &dev, _In_ const request_search &crit);

} // namespace usbip::device
