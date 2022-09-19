/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbdevice.h"
#include "trace.h"
#include "usbdevice.tmh"

#include "network.h"
#include <usbip\vhci.h>

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::create_usbdevice(_In_ WDFDEVICE, vhci::ioctl_plugin &)
{
        return STATUS_NOT_IMPLEMENTED;
}
