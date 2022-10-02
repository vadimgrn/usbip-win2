/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

#include <libdrv\codeseg.h>

namespace usbip
{
        struct device_ctx_ext;
}

namespace usbip::device
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create(_Out_ UDECXUSBDEVICE &dev, _In_ WDFDEVICE vhci, _In_ device_ctx_ext *ext);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void destroy(_In_ UDECXUSBDEVICE dev);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS schedule_destroy(_In_ UDECXUSBDEVICE dev);

} // namespace usbip::device
