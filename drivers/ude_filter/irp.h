/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "device.h"

namespace usbip
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

inline auto ForwardIrp(_In_ filter_ext &f, _In_ IRP *irp)
{
        return ForwardIrp(f.target, irp);
}

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS ForwardIrpAndWait(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

inline auto ForwardIrpAndWait(_In_ filter_ext &f, _In_ IRP *irp)
{
        return ForwardIrpAndWait(f.target, irp);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS CompleteRequest(_In_ IRP *irp, _In_ NTSTATUS status);

inline void CompleteRequest(_In_ IRP *irp)
{
        IoCompleteRequest(irp, IO_NO_INCREMENT);
}

} // namespace usbip
