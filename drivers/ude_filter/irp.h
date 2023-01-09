/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "device.h"
#include <libdrv\irp.h>

namespace usbip
{

using libdrv::CompleteRequest;

inline auto ForwardIrp(_In_ filter_ext &f, _In_ IRP *irp)
{
        return libdrv::ForwardIrp(f.target, irp);
}

inline auto ForwardIrpSynchronously(_In_ filter_ext &f, _In_ IRP *irp)
{
        return libdrv::ForwardIrpSynchronously(f.target, irp);
}

} // namespace usbip

