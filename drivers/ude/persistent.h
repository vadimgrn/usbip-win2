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

namespace vhci
{
        struct imported_device_location;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS open(_Inout_ Registry &key, _In_ DRIVER_REGKEY_TYPE type, _In_ ACCESS_MASK access = KEY_QUERY_VALUE);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS fill_location(_Inout_ vhci::imported_device_location &r, _In_ const device_attributes &attr);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS hash_location(_Inout_ ULONG &hash, _In_ const device_attributes &r);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
ObjectDelete create_request(_In_ WDFIOTARGET target, _In_ WDF_OBJECT_ATTRIBUTES &attr);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void plugin_persistent_devices(_In_ WDFDEVICE vhci);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void start_attach_attempts(
        _In_ WDFDEVICE vhci, _Inout_ vhci_ctx &vctx, _In_ const device_attributes &attr, _In_ bool delayed = false);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
int stop_attach_attempts(_Inout_ vhci_ctx &vhci, _In_ ULONG location_hash);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool can_reattach(_In_ WDFDEVICE vhci, _In_ ULONG location_hash, _In_ NTSTATUS status);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto get_next_delay(_In_ unsigned int delay, _In_ unsigned int max_delay)
{
        NT_ASSERT(delay && delay <= max_delay);

        if (delay != max_delay) {
                auto next = 3ULL*delay/2;
                delay = next < max_delay ? static_cast<unsigned int>(next) : max_delay;
        }

        return delay;
}

} // namespace usbip
