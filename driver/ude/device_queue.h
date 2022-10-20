/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>
#include <usbip\proto.h>

#include <wdf.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

namespace usbip
{
        struct device_ctx;
}

namespace usbip::device
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create_queue(_In_ UDECXUSBDEVICE dev);

struct request_search
{
        request_search(UDECXUSBENDPOINT endp) : endpoint(endp), use_endp(true) {}
        request_search(seqnum_t n) : endpoint(reinterpret_cast<UDECXUSBENDPOINT>(static_cast<uintptr_t>(n))) {}

        union {
                UDECXUSBENDPOINT endpoint{};
                seqnum_t seqnum;
                static_assert(sizeof(endpoint) >= sizeof(seqnum));
        };

        bool use_endp{};
};

/*
 * @param queue device_ctx.queue 
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST dequeue_request(_In_ device_ctx &dev, _In_ const request_search &crit);

// @see WdfIoQueueRetrieveNextRequest

} // namespace usbip::device
