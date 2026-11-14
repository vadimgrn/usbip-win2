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
struct paged_pool_traits
{
        static T* invalid() { return nullptr; }
};

/*
 * Frees untagged kernel-allocated PagedPool buffers (e.g., from IoGetDeviceInterfaces
 * or SeQueryInformationToken) using ExFreePool at IRQL <= APC_LEVEL (PASSIVE_LEVEL).
 */
template<typename T>
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED inline void close_handle(_In_ T *ptr, _In_ paged_pool_traits<T>)
{
        PAGED_CODE();
        ExFreePool(ptr);
}

template<typename T>
using paged_pool_handle = usbip::generic_handle<paged_pool_traits<T>>;

using device_interfaces_handle = paged_pool_handle<WCHAR>;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS get_device_interfaces(
        _Out_ device_interfaces_handle &result,
        _In_ const GUID &interface_class_guid,
        _In_opt_ DEVICE_OBJECT *pdo = nullptr,
        _In_ ULONG flags = 0);

} // namespace libdrv
