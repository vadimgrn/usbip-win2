/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>
#include <wdf.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

namespace usbip
{

struct device_ctx;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void complete(_In_ WDFREQUEST request);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS init_receive_usbip_header(_In_ device_ctx &ctx);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS WskDisconnectEvent(_In_opt_ PVOID SocketContext, _In_ ULONG Flags);

} // namespace usbip
