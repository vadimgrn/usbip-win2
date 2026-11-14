/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "handle.h"

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::get_device_interfaces(
        _Out_ device_interfaces_handle &result,
        _In_ const GUID &interface_class_guid,
        _In_opt_ DEVICE_OBJECT *pdo,
        _In_ ULONG flags)
{
        PAGED_CODE();

        PZZWSTR list{};
        auto st = IoGetDeviceInterfaces(&interface_class_guid, pdo, flags, &list);

        result.reset(list);
        return st;
}
