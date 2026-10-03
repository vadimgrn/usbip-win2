/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>
#include <libdrv/security.h>

#include "isolation.h"
#include "trace.h"
#include "isolation.tmh"

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED ULONG usbip::session::get_requestor_session_id(_In_ WDFREQUEST request)
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
PAGED bool usbip::session::is_admin_request(_In_ WDFREQUEST request)
{
        PAGED_CODE();

        if (WdfRequestGetRequestorMode(request) == KernelMode) {
                return true;
        }

        auto irp = WdfRequestWdmGetIrp(request);
        return irp ? libdrv::is_admin_request(irp) : false;
}

/*
 * Retrieves the effective User SID of the requestor.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::get_requestor_sid(_In_ WDFREQUEST request, _Out_ vhci::sid_data &sid)
{
        PAGED_CODE();

        auto irp = WdfRequestWdmGetIrp(request);
        return irp ? libdrv::get_requestor_sid(irp, sid) : STATUS_INVALID_PARAMETER;
}

/*
 * Checks whether the requestor matches the expected User SID.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_caller_sid(_In_ WDFREQUEST request, _In_ const vhci::sid_data &expected_sid)
{
        PAGED_CODE();

        if (WdfRequestGetRequestorMode(request) == KernelMode) {
                return true;
        }

        auto irp = WdfRequestWdmGetIrp(request);
        return irp ? libdrv::is_caller_sid(irp, expected_sid) : false;
}
