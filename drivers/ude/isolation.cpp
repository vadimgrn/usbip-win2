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

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_admin_request(_In_ WDFREQUEST request)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        return libdrv::is_admin_request(irp);
}

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED ULONG usbip::session::get_requestor_session_id(_In_ WDFREQUEST request)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        return libdrv::get_requestor_session_id(irp);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::get_requestor_owner(_In_ WDFREQUEST request, _Out_ vhci::device_owner &owner)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        owner.session_id = libdrv::get_requestor_session_id(irp);
        return libdrv::get_requestor_sid(irp, owner.sid);
}

/*
 * Evaluation order does not matter: session and user isolation modes are
 * mutually exclusive based on dev.iso_mode(), so checking session isolation
 * first cannot bypass user isolation.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_owner(_In_ const device_ctx &dev, _In_ const vhci::device_owner &owner)
{
        PAGED_CODE();

        if (dev.is_session_isolated()) {
                return dev.owner.session_id == owner.session_id;
        }

        if (dev.is_user_isolated()) {
                return dev.owner.sid == owner.sid;
        }

        return false;
}

/*
 * Evaluation order does not matter: session and user isolation modes are
 * mutually exclusive based on dev.iso_mode(), so checking session isolation
 * first cannot bypass user isolation.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::session::is_access_denied(_In_ const device_ctx &dev, _In_ const vhci::device_owner &owner)
{
        PAGED_CODE();

        if (dev.is_session_isolated()) {
                return dev.owner.session_id != owner.session_id;
        }

        if (dev.is_user_isolated()) {
                return dev.owner.sid != owner.sid;
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
