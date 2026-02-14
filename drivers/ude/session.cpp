/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>

#include "session.h"
#include "trace.h"
#include "session.tmh"

#include "context.h"

/*
NTSTATUS IoGetRequestorSessionId(IN PIRP Irp, OUT PULONG pSessionId)
{
        PEPROCESS Process;
 
        if (Irp->Tail.Overlay.Thread)
        {
                Process = Irp->Tail.Overlay.Thread->ThreadsProcess;
                *pSessionId = MmGetSessionId(Process);
                return STATUS_SUCCESS;
        }

        *pSessionId = (ULONG)-1;
        return STATUS_UNSUCCESSFUL;
}
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG usbip::get_session_id(_In_ WDFREQUEST request)
{
        ULONG session_id;

        if (auto irp = WdfRequestWdmGetIrp(request);
            NT_ERROR(IoGetRequestorSessionId(irp, &session_id))) {
                NT_ASSERT(session_id == INVALID_SESSION_ID);
        }

        return session_id;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::check_session_access(_In_ const device_ctx &dev, _In_ WDFREQUEST request)
{
        auto id = get_session_id(request);
        return check_session_access(dev, id);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::check_session_access(_In_ const device_ctx &dev, _In_ ULONG session_id)
{
        if (dev.owner_session_id == INVALID_SESSION_ID) {
                return STATUS_SUCCESS;
        }

        switch (session_id) {
        case SYSTEM_SESSION_ID:
        case INVALID_SESSION_ID: // the request does not have associated thread
                return STATUS_SUCCESS;
        default:
                return session_id == dev.owner_session_id ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
        }
}

