/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>
#include <libdrv\wdf_cpp.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

namespace usbip
{
        struct device_ctx_ext;
} // namespace usbip


namespace usbip::device
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create(_Out_ UDECXUSBDEVICE &device, _In_ WDFDEVICE vhci, _In_ device_ctx_ext *ext);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS async_plugout_and_delete(_In_ UDECXUSBDEVICE device);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS plugout_and_delete(_In_ UDECXUSBDEVICE device);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void detach(_In_ UDECXUSBDEVICE device, _In_ bool plugout_and_delete);

} // namespace usbip::device
