/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>
#include <libdrv\wdf_cpp.h>
#include <libdrv/wdm_cpp.h>

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
PAGED NTSTATUS create(_Out_ UDECXUSBDEVICE &device, _In_ WDFDEVICE vhci, _In_ WDFMEMORY ctx_ext);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS recv_thread_start(_In_ UDECXUSBDEVICE device);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED wdm::object_reference detach(_In_ UDECXUSBDEVICE device, _In_ bool plugout_and_delete, _In_ bool reattach = false);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
inline auto detach_and_delete(_In_ UDECXUSBDEVICE device, _In_ bool reattach = false)
{
        return detach(device, true, reattach);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void async_detach_and_delete(_In_ UDECXUSBDEVICE device, _In_ bool reattach = false);

} // namespace usbip::device
