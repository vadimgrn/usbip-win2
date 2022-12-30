/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device.h"
#include "trace.h"
#include "device.tmh"


_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_When_(return>=0, _Kernel_clear_do_init_(__yes))
PAGED NTSTATUS add_device(_In_ DRIVER_OBJECT*, _In_ DEVICE_OBJECT *pdo)
{
        PAGED_CODE();

        Trace(TRACE_LEVEL_INFORMATION, "pdo %04x", ptr04x(pdo));
        return STATUS_INSUFFICIENT_RESOURCES;
}
