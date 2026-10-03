/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/wdf_cpp.h>
#include <usbip/vhci.h>

namespace usbip
{

namespace session
{

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED ULONG get_requestor_session_id(_In_ WDFREQUEST request);

/*
 * Checks whether the requestor has administrative privileges (or is kernel mode).
 * Safely inspects the caller thread's impersonation token, falling back to the process primary token.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_admin_request(_In_ WDFREQUEST request);

/*
 * Retrieves the effective User SID of the requestor.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS get_requestor_sid(_In_ WDFREQUEST request, _Out_ vhci::sid_data &sid);

/*
 * Checks whether the requestor matches the expected User SID.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_caller_sid(_In_ WDFREQUEST request, _In_ const vhci::sid_data &expected_sid);

} // namespace session

} // namespace usbip
