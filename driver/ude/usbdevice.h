/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

#include <libdrv\pageable.h>

namespace usbip::usbdevice
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS create(_Out_ UDECXUSBDEVICE &udev, _In_ WDFDEVICE vhci, _In_ UDECX_USB_DEVICE_SPEED speed);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void destroy(_In_ UDECXUSBDEVICE udev);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS schedule_destroy(_In_ UDECXUSBDEVICE udev);

} // namespace usbip::usbdevice
