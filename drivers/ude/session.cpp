/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>

#include "session.h"
#include "trace.h"
#include "session.tmh"

#include <usbip/consts.h>

/*
 * The Terminal Server session that issued the request, or invalid_session_id if it cannot be
 * determined (e.g. a kernel-mode request), which denies ownership by design.
 *
 * ReactOS sources:
 * NTSTATUS NTAPI IoGetRequestorSessionId(IN PIRP Irp, OUT PULONG pSessionId)
 * {
 *         PEPROCESS Process;
 *
 *         if (Irp->Tail.Overlay.Thread)
 *         {
 *                 Process = Irp->Tail.Overlay.Thread->ThreadsProcess;
 *                 *pSessionId = MmGetSessionId(Process);
 *                 return STATUS_SUCCESS;
 *         }
 *
 *         *pSessionId = (ULONG)-1;
 *         return STATUS_UNSUCCESSFUL;
 * }
 */
_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
ULONG usbip::session::get_requestor_session_id(_In_ WDFREQUEST request)
{
        ULONG id;
        auto irp = WdfRequestWdmGetIrp(request);
        return NT_SUCCESS(IoGetRequestorSessionId(irp, &id)) ? id : invalid_session_id;
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
        if (!irp) {
                return false;
        }

        if (auto thread = irp->Tail.Overlay.Thread) {
                BOOLEAN copy_on_open{};
                BOOLEAN effective_only{};
                SECURITY_IMPERSONATION_LEVEL level{};
                if (auto token = PsReferenceImpersonationToken(thread, &copy_on_open, &effective_only, &level)) {
                        auto is_admin = SeTokenIsAdmin(token);
                        PsDereferenceImpersonationToken(token);
                        return is_admin;
                }
        }

        if (auto process = IoGetRequestorProcess(irp)) {
                if (auto token = PsReferencePrimaryToken(process)) {
                        auto is_admin = SeTokenIsAdmin(token);
                        PsDereferencePrimaryToken(token);
                        return is_admin;
                }
        }

        return false;
}
