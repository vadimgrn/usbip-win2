/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/wdf_cpp.h>
#include <libdrv/security.h>

namespace usbip::session
{

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED inline auto get_requestor_session_id(_In_ WDFREQUEST request)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        return libdrv::get_requestor_session_id(irp);
}

/*
 * Checks whether the requestor has administrative privileges (or is kernel mode).
 * Safely inspects the caller thread's impersonation token, falling back to the process primary token.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED inline auto is_admin_request(_In_ WDFREQUEST request)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        return libdrv::is_admin_request(irp);
}

/*
 * Retrieves the effective User SID of the requestor.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED inline auto get_requestor_sid(_In_ WDFREQUEST request, _Out_ vhci::sid_data &sid)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        return libdrv::get_requestor_sid(irp, sid);
}

/*
 * Retrieves the effective owner (session ID and User SID) of the requestor.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED inline auto get_requestor_owner(_In_ WDFREQUEST request)
{
        PAGED_CODE();
        vhci::device_owner owner;
        owner.session_id = get_requestor_session_id(request);
        get_requestor_sid(request, owner.sid);
        return owner;
}

/*
 * Checks whether the requestor matches the expected User SID.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED inline auto is_caller_sid(_In_ WDFREQUEST request, _In_ const vhci::sid_data &expected_sid)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        return libdrv::is_caller_sid(irp, expected_sid);
}

} // namespace usbip::session
