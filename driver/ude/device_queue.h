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

namespace usbip::device
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create_queue(_In_ UDECXUSBDEVICE dev);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST dequeue(_In_ UDECXUSBDEVICE dev, _In_ seqnum_t seqnum);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST dequeue(_In_ UDECXUSBDEVICE dev, _In_ USBD_PIPE_HANDLE handle);

// @see WdfIoQueueRetrieveNextRequest

} // namespace usbip::device
