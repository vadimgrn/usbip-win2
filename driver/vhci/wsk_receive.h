/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <ntdef.h>

struct vpdo_dev_t;
struct wsk_context;

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS WskDisconnectEvent(_In_opt_ PVOID SocketContext, _In_ ULONG Flags);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS sched_read_usbip_header(_In_opt_ vpdo_dev_t *vpdo, _In_opt_ wsk_context *ctx);
