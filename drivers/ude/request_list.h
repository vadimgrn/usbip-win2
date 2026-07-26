/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create_request_completion_dpc(_In_ UDECXUSBDEVICE device, _Inout_ device_ctx &dev);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS initialize_request(_Inout_ device_ctx &dev, _In_ WDFREQUEST request, _In_ UDECXUSBENDPOINT endpoint);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void append_request(_Inout_ device_ctx &dev, _In_ const wsk_context &wsk);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS on_send_complete(_Inout_ device_ctx &dev, _In_ WDFREQUEST request, _In_ NTSTATUS send_status);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST begin_response(_Inout_ device_ctx &dev, _In_ seqnum_t seqnum);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void finish_response(_In_ WDFREQUEST request, _In_ NTSTATUS status);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void finish_request(_In_ WDFREQUEST request, _In_ NTSTATUS status);

} // namespace usbip::device
