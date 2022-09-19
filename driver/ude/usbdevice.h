/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <libdrv\pageable.h>

namespace usbip
{

namespace vhci 
{
        struct ioctl_plugin;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS create_usbdevice(_In_ WDFDEVICE vhci, vhci::ioctl_plugin &r);

} // namespace usbip
