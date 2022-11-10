/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>
#include <libdrv/wdf_cpp.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

struct usbip_usb_device;

namespace usbip
{
        struct device_ctx_ext;
}

namespace usbip::device
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create(_Out_ UDECXUSBDEVICE &dev, _In_ const usbip_usb_device &udev, _In_ WDFDEVICE vhci, _In_ device_ctx_ext *ext);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void plugout_and_delete(_In_ UDECXUSBDEVICE dev);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS sched_plugout_and_delete(_In_ UDECXUSBDEVICE dev);

} // namespace usbip::device
