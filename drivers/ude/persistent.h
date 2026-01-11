/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>
#include <libdrv\wdf_cpp.h>

namespace usbip
{

struct vhci_ctx;
struct device_attributes;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS open(_Inout_ Registry &key, _In_ DRIVER_REGKEY_TYPE type, _In_ ACCESS_MASK access = KEY_QUERY_VALUE);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS copy(
        _Inout_ char *host, _In_ USHORT host_sz, _In_ const UNICODE_STRING &uhost,
        _Inout_ char *service, _In_ USHORT service_sz, _In_ const UNICODE_STRING &uservice,
        _Inout_ char *busid, _In_ USHORT busid_sz, _In_ const UNICODE_STRING &ubusid);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS make_device_str(
        _Inout_ WDFSTRING &device_str, _In_ WDFOBJECT parent, _In_ const device_attributes &r);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
ObjectDelete create_request(_In_ WDFIOTARGET target, _In_ WDF_OBJECT_ATTRIBUTES &attr);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void plugin_persistent_device(
        _In_ WDFDEVICE vhci, _Inout_ vhci_ctx &vctx, _In_ WDFSTRING device_str, _In_ bool delayed = false);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void plugin_persistent_devices(_In_ WDFDEVICE vhci);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool can_reattach(_In_ NTSTATUS status);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void cancel_reattach_requests(_Inout_ vhci_ctx &vhci, _In_opt_ WDFSTRING device_str);

} // namespace usbip
