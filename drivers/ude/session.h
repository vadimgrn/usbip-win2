/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/wdf_cpp.h>

namespace usbip::session
{

/*
 * The Terminal Server session that issued the request, or invalid_session_id if it cannot be
 * determined (e.g. a kernel-mode request), which denies ownership by design.
 */
_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
ULONG get_requestor_session_id(_In_ WDFREQUEST request);

/*
 * Checks whether the requestor has administrative privileges (or is kernel mode).
 * Safely inspects the caller thread's impersonation token, falling back to the process primary token.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_admin_request(_In_ WDFREQUEST request);

} // namespace usbip::session
