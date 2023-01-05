/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "device.h"

namespace usbip
{

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS ForwardIrp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

inline auto ForwardIrp(_In_ filter_ext &f, _In_ IRP *irp)
{
        return ForwardIrp(f.lower, irp);
}

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS ForwardIrpAndWait(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

inline auto ForwardIrpAndWait(_In_ filter_ext &f, _In_ IRP *irp)
{
        return ForwardIrpAndWait(f.lower, irp);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS CompleteRequest(_In_ IRP *irp, _In_ NTSTATUS status = STATUS_SUCCESS);

} // namespace usbip
