/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/wdf_cpp.h>

namespace usbip
{

struct device_ctx;

enum : ULONG {
        SYSTEM_SESSION_ID, // system services and background processes
        INVALID_SESSION_ID = ULONG(-1),
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG get_session_id(_In_ WDFREQUEST request);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS check_session_access(_In_ const device_ctx &dev, _In_ WDFREQUEST request);

/*
 * Session isolation ensures that devices attached in one
 * terminal session are only accessible from that same session.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS check_session_access(_In_ const device_ctx &dev, _In_ ULONG session_id);

} // namespace usbip
