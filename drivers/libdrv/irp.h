/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "codeseg.h"

namespace libdrv
{

template<auto N, typename T = void>
inline auto& argv(_In_ IRP *irp)
{
        auto &ptr = irp->Tail.Overlay.DriverContext[N];
        return reinterpret_cast<T*&>(ptr);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS ForwardIrp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS ForwardIrpSynchronously(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS CompleteRequest(_In_ IRP *irp, _In_ NTSTATUS status);

inline void CompleteRequest(_In_ IRP *irp)
{
        IoCompleteRequest(irp, IO_NO_INCREMENT);
}

} // namespace libdrv
