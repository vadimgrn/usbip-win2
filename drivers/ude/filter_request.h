/*
 * Copyright (C) 2023 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
struct _URB_CONTROL_TRANSFER_EX;

namespace usbip 
{
        struct device_ctx;
}

namespace usbip::filter
{

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS unpack_request(_In_ device_ctx &dev, _Inout_ _URB_CONTROL_TRANSFER_EX &r, _In_ int function);

} // namespace usbip::filter
