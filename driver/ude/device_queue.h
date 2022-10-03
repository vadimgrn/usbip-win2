/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>
#include <usbip\proto.h>

#include "context.h"

#include <wdf.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

namespace usbip::device
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create_queue(_In_ UDECXUSBDEVICE dev);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST dequeue_request(_In_ WDFQUEUE queue, _In_ const request_ctx &crit);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST dequeue_request(_In_ UDECXUSBDEVICE dev, _In_ const request_ctx &crit);

// @see WdfIoQueueRetrieveNextRequest

} // namespace usbip::device
