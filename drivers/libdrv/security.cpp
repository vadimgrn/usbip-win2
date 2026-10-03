/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>
#include "security.h"

#include <usbip/consts.h>
#include <libusbip/generic_handle_ex.h>

using namespace usbip;

namespace
{

static_assert(sizeof(vhci::sid_data::data) == SECURITY_MAX_SID_SIZE);

struct token_traits
{
        static PACCESS_TOKEN invalid() { return nullptr; }
};

/*
 * While ObDereferenceObject in general accepts IRQL <= DISPATCH_LEVEL, an ACCESS_TOKEN
 * is an Executive Object allocated from PagedPool. Dropping the token's reference count
 * to zero synchronously invokes its deletion procedure (SepTokenDeleteMethod), which
 * accesses and frees paged memory and therefore requires PASSIVE_LEVEL.
 *
 * ObDereferenceObject works identically for both primary and impersonation tokens
 * (as explicitly documented by Microsoft for PsReferenceImpersonationToken), avoiding
 * the need to query token types with reserved routines like SeTokenType.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED inline void close_handle(_In_ PACCESS_TOKEN token, _In_ token_traits)
{
        PAGED_CODE();
        ObDereferenceObject(token);
}

using token_handle = generic_handle<token_traits>;

struct token_user_traits
{
        static TOKEN_USER* invalid() { return nullptr; }
};

/*
 * SeQueryInformationToken allocates the TOKEN_USER buffer from PagedPool.
 * It must be freed using ExFreePool at IRQL <= APC_LEVEL (PASSIVE_LEVEL).
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED inline void close_handle(_In_ TOKEN_USER *ptr, _In_ token_user_traits)
{
        PAGED_CODE();
        ExFreePool(ptr);
}

using token_user_handle = generic_handle<token_user_traits>;

/*
 * References the caller's effective access token.
 *
 * In Windows, a thread only holds an impersonation token if it is actively
 * impersonating another security principal (e.g., via RPC or named pipe
 * impersonation). For standard, non-impersonating callers,
 * PsReferenceImpersonationToken returns NULL by design.
 *
 * When a thread is not impersonating, its effective security context is the
 * process's primary token. We obtain the originating process via
 * IoGetRequestorProcess (which properly resolves process attachments and
 * file-object creators) and reference its primary token.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED token_handle reference_caller_token(_In_ IRP *irp)
{
        PAGED_CODE();

        if (auto thread = irp->Tail.Overlay.Thread) {
                BOOLEAN copy_on_open{};
                BOOLEAN effective_only{};
                SECURITY_IMPERSONATION_LEVEL level{};
                if (auto token = PsReferenceImpersonationToken(thread, &copy_on_open, &effective_only, &level)) {
                        return token_handle(token);
                }
        }

        if (auto process = IoGetRequestorProcess(irp)) {
                if (auto token = PsReferencePrimaryToken(process)) {
                        return token_handle(token);
                }
        }

        return {};
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::get_requestor_sid(_In_ IRP *irp, _Out_ sid_data &sid)
{
        PAGED_CODE();
        sid = {};

        if (!irp) {
                return STATUS_INVALID_PARAMETER;
        }

        auto token = reference_caller_token(irp);
        if (!token) {
                return STATUS_NO_TOKEN;
        }

        TOKEN_USER *tu{};
        auto st = SeQueryInformationToken(token.get(), TokenUser, reinterpret_cast<PVOID*>(&tu));
        if (!NT_SUCCESS(st)) {
                return st;
        }
        token_user_handle del(tu);

        if (!(tu && tu->User.Sid && RtlValidSid(tu->User.Sid))) {
                return STATUS_INVALID_SID;
        }

        auto sid_len = RtlLengthSid(tu->User.Sid);
        if (sid_len > sizeof(sid.data)) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        RtlCopyMemory(sid.data, tu->User.Sid, sid_len);
        sid.length = sid_len;

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool libdrv::is_caller_sid(_In_ IRP *irp, _In_ const sid_data &expected_sid)
{
        PAGED_CODE();

        if (!(irp && expected_sid.length)) {
                return false;
        }

        if (irp->RequestorMode == KernelMode) {
                return true;
        }

        sid_data caller_sid;

        return NT_SUCCESS(get_requestor_sid(irp, caller_sid)) &&
               equal_sid(caller_sid, expected_sid);
}

/*
 * SeExports is an exported global structure from ntoskrnl.exe.
 * During early Phase 0 kernel boot (inside SepVariableInitialization),
 * Security Reference Monitor statically allocates and initializes
 * the well-known system SIDs (including SeLocalSystemSid for S-1-5-18,
 * SeWorldSid, SeAnonymousLogonSid, etc.) long before any driver is loaded.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool libdrv::is_system_sid(_In_ const sid_data &sid)
{
        NT_ASSERT(SeExports);
        NT_ASSERT(SeExports->SeLocalSystemSid);

        return  sid.length &&
                RtlValidSid(const_cast<UCHAR*>(sid.data)) &&
                RtlEqualSid(const_cast<UCHAR*>(sid.data), SeExports->SeLocalSystemSid);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool libdrv::is_system_request(_In_ IRP *irp)
{
        PAGED_CODE();

        if (!irp) {
                return false;
        }

        if (irp->RequestorMode == KernelMode) {
                return true;
        }

        sid_data caller_sid;

        return  NT_SUCCESS(get_requestor_sid(irp, caller_sid)) &&
                is_system_sid(caller_sid);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool libdrv::is_admin_request(_In_ IRP *irp)
{
        PAGED_CODE();

        if (!irp) {
                return false;
        }

        if (irp->RequestorMode == KernelMode) {
                return true;
        }

        auto token = reference_caller_token(irp);
        return token && SeTokenIsAdmin(token.get());
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool libdrv::equal_sid(_In_ const sid_data &a, _In_ const sid_data &b)
{
        return  a.length &&
                b.length &&
                a.length == b.length &&
                RtlValidSid(const_cast<UCHAR*>(a.data)) &&
                RtlValidSid(const_cast<UCHAR*>(b.data)) &&
                RtlEqualSid(const_cast<UCHAR*>(a.data), const_cast<UCHAR*>(b.data));
}

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
PAGED ULONG libdrv::get_requestor_session_id(_In_ IRP *irp)
{
        PAGED_CODE();
        NT_ASSERT(irp);

        ULONG id = invalid_session_id;
        if (!NT_SUCCESS(IoGetRequestorSessionId(irp, &id))) {
                NT_ASSERT(id == invalid_session_id);
        }
        return id;
}
