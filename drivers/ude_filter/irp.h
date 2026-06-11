/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "device.h"
#include <libdrv\irp.h>

namespace usbip
{

using libdrv::CompleteRequest;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto ForwardIrp(_In_ filter_ext &f, _In_ IRP *irp)
{
        return libdrv::ForwardIrp(f.target, irp);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto ForwardIrpSynchronously(_In_ filter_ext &f, _In_ IRP *irp)
{
        return libdrv::ForwardIrpSynchronously(f.target, irp);
}

} // namespace usbip

