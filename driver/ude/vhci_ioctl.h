/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <libdrv\codeseg.h>

namespace usbip::vhci
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create_default_queue(_In_ WDFDEVICE vhci);

} // namespace usbip::vhci
