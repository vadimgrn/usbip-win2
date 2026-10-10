/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "codeseg.h"
#include <libusbip/generic_handle_ex.h>

#include <wdm.h>

namespace libdrv
{

struct zw_handle_traits
{
        static HANDLE invalid() { return nullptr; }
};

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED inline void close_handle(_In_ HANDLE h, _In_ zw_handle_traits)
{
        PAGED_CODE();
        NT_VERIFY(NT_SUCCESS(ZwClose(h)));
}

using zw_handle = usbip::generic_handle<zw_handle_traits>;


template<typename T>
struct pool_traits
{
        static T* invalid() { return nullptr; }
};

/*
 * Frees untagged kernel-allocated buffers (e.g., from IoGetDeviceInterfaces
 * or SeQueryInformationToken) using ExFreePool.
 * IRQL <= DISPATCH_LEVEL for NonPagedPool, <= APC_LEVEL for PagedPool.
 */
template<typename T>
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void close_handle(_In_ T *ptr, _In_ pool_traits<T>)
{
        ExFreePool(ptr);
}

template<typename T>
using pool_ptr = usbip::generic_handle<pool_traits<T>>;

using device_interfaces_ptr = pool_ptr<WCHAR>;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS get_device_interfaces(
        _Out_ device_interfaces_ptr &result,
        _In_ const GUID &interface_class_guid,
        _In_opt_ DEVICE_OBJECT *pdo = nullptr,
        _In_ ULONG flags = 0);

} // namespace libdrv
