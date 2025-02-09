/*
 * Copyright (C) 2023 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>

struct _USB_BUS_INTERFACE_USBDI_V3;

namespace usbip
{

struct filter_ext;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void query_interface(_Inout_ filter_ext &fltr, _Inout_ _USB_BUS_INTERFACE_USBDI_V3 &v3);

} // namespace usbip
