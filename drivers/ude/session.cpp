/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>

#include "session.h"
#include "trace.h"
#include "session.tmh"

#include "context.h"
#include <devpkey.h>

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

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS usbip::get_session_id(_Inout_ ULONG &session_id, _In_ WDFDEVICE dev)
{
        PAGED_CODE();

        WDF_DEVICE_PROPERTY_DATA pd;
        WDF_DEVICE_PROPERTY_DATA_INIT(&pd, &DEVPKEY_Device_SessionId);

        ULONG actual;
        DEVPROPTYPE type;

        if (auto err = WdfDeviceQueryPropertyEx(dev, &pd, sizeof(session_id), &session_id, &actual, &type)) {
                Trace(TRACE_LEVEL_ERROR, "WdfDeviceQueryPropertyEx(Device_SessionId) %!STATUS!", err);
                return err;
        }

        NT_ASSERT(actual == sizeof(session_id));
        NT_ASSERT(type == DEVPROP_TYPE_UINT32);

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS usbip::set_session_id(_In_ WDFDEVICE dev, _In_ ULONG session_id)
{
        PAGED_CODE();
        NT_ASSERT(is_valid_user_session_id(session_id));

        WDF_DEVICE_PROPERTY_DATA pd;
        WDF_DEVICE_PROPERTY_DATA_INIT(&pd, &DEVPKEY_Device_SessionId);
        pd.Lcid = LOCALE_NEUTRAL;

        if (auto err = WdfDeviceAssignProperty(dev, &pd, DEVPROP_TYPE_UINT32, sizeof(session_id), &session_id)) {
                Trace(TRACE_LEVEL_ERROR, "WdfDeviceAssignProperty(Device_SessionId=%lx) %!STATUS!", session_id, err);
                return err;
        }

        return STATUS_SUCCESS;
}

