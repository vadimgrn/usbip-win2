/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "isolation.h"
#include "trace.h"
#include "isolation.tmh"

#include "context.h"
#include "device.h"
#include "persistent.h"

#include <libdrv/security.h>

using namespace usbip;
using namespace libdrv;

namespace
{

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED auto get_requestor_session_id(_In_ WDFREQUEST request)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        return libdrv::get_requestor_session_id(irp);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_requestor_owner(_In_ WDFREQUEST request, _Out_ vhci::device_owner &owner)
{
        PAGED_CODE();
        auto irp = WdfRequestWdmGetIrp(request);
        owner.session_id = libdrv::get_requestor_session_id(irp);
        return libdrv::get_requestor_sid(irp, owner.sid);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto init_session_isolation(_In_ WDFREQUEST request, _Inout_ vhci::device_owner &owner)
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
PAGED auto init_user_isolation(_In_ WDFREQUEST request, _Inout_ vhci::device_owner &owner)
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

_Function_class_(IO_SESSION_NOTIFICATION_FUNCTION)
_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS NTAPI session_notification_cb(
        _In_ void *session_obj,
        _In_ void* /*io_obj*/,
        _In_ ULONG event,
        _In_ void *context,
        _In_reads_bytes_opt_(payload_len) void* /*payload*/,
        [[maybe_unused]] _In_ ULONG payload_len)
{
        PAGED_CODE();

        if (!(event == IoSessionEventLogoff || event == IoSessionEventTerminated)) {
                return STATUS_SUCCESS;
        }

        auto vhci_dev = static_cast<WDFDEVICE>(context);
        NT_ASSERT(vhci_dev);

        auto &vhci = *get_vhci_ctx(vhci_dev);
        if (get_flag(vhci.removing)) {
                return STATUS_SUCCESS;
        }

        IO_SESSION_STATE_INFORMATION info;
        auto st = IoGetContainerInformation(IoSessionStateInformation, session_obj, &info, sizeof(info));
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "IoGetContainerInformation %!STATUS!", st);
                return STATUS_SUCCESS;
        }

        vhci::device_owner owner{ .session_id = info.SessionId };
        if (!owner.has_session()) {
                return STATUS_SUCCESS;
        }

        auto stopped = stop_attach_attempts(vhci, 0, &owner);

        Trace(TRACE_LEVEL_INFORMATION, "Session %lu, event %lu, stopped %d attach attempt(s)",
                                        owner.session_id, event, stopped);

        async_detach_and_delete_all(vhci_dev, &owner);
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
PAGED NTSTATUS usbip::session::init_requestor_isolation(
        _In_ WDFREQUEST request, _Inout_ vhci::isolation_data &iso)
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
                if (dev.owner().session_id != owner.session_id) {
                        return false;
                }
                auto &user_sid = dev.attributes().caller_user_sid;
                return !(owner.has_sid() && libdrv::is_valid(user_sid)) || user_sid == owner.sid;
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
                if (dev.owner().session_id != owner.session_id) {
                        return true;
                }
                auto &user_sid = dev.attributes().caller_user_sid;
                return libdrv::is_valid(user_sid) && user_sid != owner.sid;
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
PAGED bool usbip::session::is_access_denied(_In_ const device_attributes &attr, _In_ const fileobject_ctx &fobj)
{
        PAGED_CODE();
        NT_ASSERT(!fobj.is_admin);

        auto &owner = attr.config.iso.owner;

        if (owner.has_sid()) {
                return owner.sid != fobj.owner.sid;
        }

        if (owner.has_session()) {
                if (owner.session_id != fobj.owner.session_id) {
                        return true;
                }
                return libdrv::is_valid(attr.caller_user_sid) && attr.caller_user_sid != fobj.owner.sid;
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

/*
 * attr.config.iso is mandatory for USB device isolation; caller_user_sid is also
 * captured for user-mode isolation::none attaches (per-user port quota accounting)
 * and caller_logon_sid is stored for volume DACL hardening (logon SIDs may be absent
 * and empty SIDs are handled by volume isolation).
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::init_requestor_isolation(
        _In_ WDFREQUEST request, _Inout_ device_attributes &attr)
{
        PAGED_CODE();

        auto st = init_requestor_isolation(request, attr.config.iso);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        auto &iso = attr.config.iso;

        if (WdfRequestGetRequestorMode(request) == KernelMode) {
                if (iso.mode == vhci::isolation::user && !libdrv::is_valid(attr.caller_user_sid)) {
                        attr.caller_user_sid = iso.owner.sid;
                }
                return STATUS_SUCCESS;
        }

        attr.caller_user_sid = {};
        attr.caller_logon_sid = {};

        auto irp = WdfRequestWdmGetIrp(request);

        if (iso.mode == vhci::isolation::user) {
                attr.caller_user_sid = iso.owner.sid;
                if (libdrv::is_caller_sid(irp, attr.caller_user_sid)) {
                        libdrv::get_requestor_logon_sid(irp, attr.caller_logon_sid);
                }
        } else if (iso.mode == vhci::isolation::session) {
                st = libdrv::get_requestor_sid(irp, attr.caller_user_sid);
                if (!NT_SUCCESS(st)) {
                        Trace(TRACE_LEVEL_ERROR, "get_requestor_sid %!STATUS!", st);
                        return STATUS_ACCESS_DENIED;
                }
                libdrv::get_requestor_logon_sid(irp, attr.caller_logon_sid);
        } else {
                libdrv::get_requestor_sid(irp, attr.caller_user_sid);
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::session::register_session_notification(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();
        auto &ctx = *get_vhci_ctx(vhci);

        IO_SESSION_STATE_NOTIFICATION note {
                .Size = sizeof(note),
                .Flags = 0,
                .IoObject = WdfDeviceWdmGetDeviceObject(vhci),
                .EventMask = IO_SESSION_STATE_LOGOFF_EVENT | IO_SESSION_STATE_TERMINATION_EVENT,
                .Context = vhci,
        };

        auto st = IoRegisterContainerNotification(
                        IoSessionStateNotification,
                        reinterpret_cast<PIO_CONTAINER_NOTIFICATION_FUNCTION>(session_notification_cb),
                        &note, sizeof(note), &ctx.session_notification);

        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "IoRegisterContainerNotification %!STATUS!", st);
        }

        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::session::unregister_session_notification(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        if (auto &ctx = *get_vhci_ctx(vhci);
            auto h = InterlockedExchangePointer(&ctx.session_notification, nullptr)) {
                IoUnregisterContainerNotification(h);
        }
}
