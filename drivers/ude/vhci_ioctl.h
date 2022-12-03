/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>
#include <libdrv/wdf_cpp.h>

namespace usbip::vhci
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS create_default_queue(_In_ WDFDEVICE vhci);

} // namespace usbip::vhci
