/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "isolation.h"
#include "trace.h"
#include "isolation.tmh"

#include "context.h"
#include <libdrv/security.h>

using namespace usbip;
using namespace libdrv;

namespace
{

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED ULONG get_requestor_session_id(_In_ WDFREQUEST request)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        return libdrv::get_requestor_session_id(irp);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS get_requestor_owner(_In_ WDFREQUEST request, _Out_ vhci::device_owner &owner)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        owner.session_id = libdrv::get_requestor_session_id(irp);
        return libdrv::get_requestor_sid(irp, owner.sid);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS init_session_isolation(_In_ WDFREQUEST request, _Inout_ vhci::device_owner &owner)
{
        PAGED_CODE();

        if (WdfRequestGetRequestorMode(request) == KernelMode) {
                if (!owner.has_session()) {
                        Trace(TRACE_LEVEL_ERROR, "session_id is required for session isolation");
                        return STATUS_INVALID_PARAMETER;
                }
                return STATUS_SUCCESS;
        }

        owner = { .session_id = get_requestor_session_id(request) };
        if (!owner.has_session()) {
                Trace(TRACE_LEVEL_ERROR, "get_requestor_session_id");
                return STATUS_ACCESS_DENIED;
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS init_user_isolation(_In_ WDFREQUEST request, _Inout_ vhci::device_owner &owner)
{
        PAGED_CODE();

        if (WdfRequestGetRequestorMode(request) == KernelMode) {
                if (!libdrv::is_valid(owner.sid)) {
                        Trace(TRACE_LEVEL_ERROR, "valid SID is required for user isolation");
                        return STATUS_INVALID_PARAMETER;
                }
                return STATUS_SUCCESS;
        }

        if (!owner.has_sid()) {
                auto st = get_requestor_owner(request, owner);
                if (!NT_SUCCESS(st)) {
                        Trace(TRACE_LEVEL_ERROR, "get_requestor_owner %!STATUS!", st);
                        return STATUS_ACCESS_DENIED;
                }
                return STATUS_SUCCESS;
        }

        if (!libdrv::is_valid(owner.sid)) {
                Trace(TRACE_LEVEL_ERROR, "invalid SID");
                return STATUS_INVALID_PARAMETER;
        }

        auto irp = WdfRequestWdmGetIrp(request);
        if (!(libdrv::is_admin_request(irp) || is_caller_sid(irp, owner.sid))) {
                Trace(TRACE_LEVEL_ERROR, "caller is not admin and SID does not match");
                return STATUS_ACCESS_DENIED;
        }

        owner.session_id = libdrv::get_requestor_session_id(irp);
        return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_admin_request(_In_ WDFREQUEST request)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        return libdrv::is_admin_request(irp);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::init_requestor_isolation(_In_ WDFREQUEST request, _Inout_ vhci::isolation_data &iso)
{
        PAGED_CODE();
        using vhci::isolation;

        if (iso.mode != isolation::user && iso.owner.has_sid()) {
                Trace(TRACE_LEVEL_ERROR, "SID specified without user isolation");
                return STATUS_INVALID_PARAMETER;
        }

        switch (iso.mode) {
        case isolation::none:
                iso.owner = {};
                return STATUS_SUCCESS;
        case isolation::session:
                return init_session_isolation(request, iso.owner);
        case isolation::user:
                return init_user_isolation(request, iso.owner);
        }

        return STATUS_INVALID_PARAMETER;
}

/*
 * Resolves caller ownership and privilege.
 * For administrators (including KernelMode callers), only the session ID is recorded;
 * extracting a User SID is unnecessary because administrators bypass user isolation
 * restrictions, and would fail with STATUS_NO_TOKEN for KernelMode threads lacking tokens.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::get_caller_owner(
        _In_ WDFREQUEST request, _Out_ bool &is_admin, _Out_ vhci::device_owner &owner)
{
        PAGED_CODE();
        owner = {};

        is_admin = is_admin_request(request);
        if (is_admin) {
                owner.session_id = get_requestor_session_id(request);
                return STATUS_SUCCESS;
        }

        auto st = get_requestor_owner(request, owner);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "get_requestor_owner %!STATUS!", st);
        }
        return st;
}

/*
 * Evaluation order does not matter: session and user isolation modes are
 * mutually exclusive based on config.iso.mode, so checking session isolation
 * first cannot bypass user isolation.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_owner(_In_ const device_ctx &dev, _In_ const vhci::device_owner &owner)
{
        PAGED_CODE();

        if (dev.is_session_isolated()) {
                return dev.owner().session_id == owner.session_id;
        }

        if (dev.is_user_isolated()) {
                return dev.owner().sid == owner.sid;
        }

        return false;
}

/*
 * Evaluation order does not matter: session and user isolation modes are
 * mutually exclusive based on config.iso.mode, so checking session isolation
 * first cannot bypass user isolation.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_access_denied(_In_ const device_ctx &dev, _In_ const vhci::device_owner &owner)
{
        PAGED_CODE();

        if (dev.is_session_isolated()) {
                return dev.owner().session_id != owner.session_id;
        }

        if (dev.is_user_isolated()) {
                return dev.owner().sid != owner.sid;
        }

        return false;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_access_denied(_In_ const device_ctx &dev, _In_ const fileobject_ctx &fobj)
{
        PAGED_CODE();
        NT_ASSERT(!fobj.is_admin);
        return is_access_denied(dev, fobj.owner);
}

/*
 * Evaluation order matters: User SID takes precedence over Session ID.
 * User-isolated owners possess both a Session ID and a User SID; checking
 * has_session() first would match on session ID and bypass SID verification.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_access_denied(_In_ const vhci::device_owner &owner, _In_ const fileobject_ctx &fobj)
{
        PAGED_CODE();
        NT_ASSERT(!fobj.is_admin);

        if (owner.has_sid()) {
                return owner.sid != fobj.owner.sid;
        }

        if (owner.has_session()) {
                return owner.session_id != fobj.owner.session_id;
        }

        return false;
}

/*
 * Evaluation order does not affect logical correctness (both session ID and
 * User SID must match to grant access), but checking session ID first allows
 * cross-session requests to short-circuit quickly before the more expensive
 * token query and SID comparison.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_access_denied(_In_ const fileobject_ctx &fobj, _In_ WDFREQUEST request)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);

        if (fobj.owner.session_id != libdrv::get_requestor_session_id(irp)) {
                return true;
        }

        if (fobj.owner.has_sid()) {
                return !libdrv::is_caller_sid(irp, fobj.owner.sid);
        }

        return false;
}
