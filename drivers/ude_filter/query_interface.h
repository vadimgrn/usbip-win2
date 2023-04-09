/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>

struct _USB_BUS_INTERFACE_USBDI_V3;

namespace usbip
{

struct filter_ext;

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
PAGED NTSTATUS replace_interface(_Inout_ _USB_BUS_INTERFACE_USBDI_V3 &v3, _In_ filter_ext &fltr);

} // namespace usbip
