/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <ntdef.h>

namespace usbip
{

struct wsk_context;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void sched_receive_usbip_header(_In_ wsk_context *ctx);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS WskDisconnectEvent(_In_opt_ PVOID SocketContext, _In_ ULONG Flags);

} // namespace usbip
