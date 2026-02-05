/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/wdf_cpp.h>

namespace usbip
{

enum : ULONG {
        INVALID_SESSION_ID = ULONG(-1),
        NULL_SESSION_ID = INVALID_SESSION_ID,
        SYSTEM_SESSION_ID = 0, // system services and background processes
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG get_session_id(_In_ WDFREQUEST request);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS check_session_access(_In_ WDFREQUEST request, _In_ ULONG expected_session_id);

/*
 * Session isolation ensures that devices attached in one
 * terminal session are only accessible from that same session.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto check_session_access(_In_ ULONG session_id, _In_ ULONG expected_session_id)
{
        NT_ASSERT(expected_session_id != INVALID_SESSION_ID);

        switch (session_id) {
        case SYSTEM_SESSION_ID:
        case INVALID_SESSION_ID: // the request does not have associated thread
                static_assert(NULL_SESSION_ID == INVALID_SESSION_ID);
                return STATUS_SUCCESS;
        default:
                return session_id == expected_session_id ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
        }
}

} // namespace usbip
