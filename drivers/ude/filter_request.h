/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
struct _URB_CONTROL_TRANSFER_EX;

namespace usbip::filter
{

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void unpack_request(_Inout_ _URB_CONTROL_TRANSFER_EX &r);

} // namespace usbip::filter
