/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>

#include "security.h"
#include "strconv.h"

#include <usbip/hex.h>
#include <usbip/vhci.h>

#include <libusbip/generic_handle_ex.h>

using namespace usbip;

namespace
{

constexpr auto check_sid_length_range(_In_ ULONG bytes)
{
        static_assert(sizeof(vhci::sid_data::data) == SECURITY_MAX_SID_SIZE);

        return  bytes >= offsetof(SID, SubAuthority) &&
                bytes <= sizeof(vhci::sid_data::data);
}

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
                BOOLEAN copy_on_open;
                BOOLEAN effective_only;
                SECURITY_IMPERSONATION_LEVEL level;
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
PAGED NTSTATUS libdrv::get_requestor_sid(_In_ IRP *irp, _Out_ sid_data &result)
{
        PAGED_CODE();
        result = {};

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
        auto sid = tu->User.Sid;

        if (!(sid && RtlValidSid(sid))) {
                return STATUS_INVALID_SID;
        }

        auto sid_len = RtlLengthSid(sid);
        if (sid_len > sizeof(result.data)) {
                return STATUS_BUFFER_TOO_SMALL;
        }

        RtlCopyMemory(result.data, sid, sid_len);
        result.length = sid_len;

        NT_ASSERT(is_valid_sid(result));
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool libdrv::is_caller_sid(_In_ IRP *irp, _In_ const sid_data &expected_sid)
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
                equal_sid(caller_sid, expected_sid);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool libdrv::is_valid_sid(_In_ const sid_data &sid)
{
        return  check_sid_length_range(sid.length) &&
                RtlValidSid(const_cast<UCHAR*>(sid.data)) &&
                sid.length == RtlLengthSid(const_cast<UCHAR*>(sid.data));
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

        return  is_valid_sid(sid) &&
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
        return  a.length == b.length &&
                is_valid_sid(a) &&
                is_valid_sid(b) &&
                RtlEqualSid(const_cast<UCHAR*>(a.data), const_cast<UCHAR*>(b.data));
}

/*
 * The Terminal Server session that issued the request, or session::invalid if it cannot be
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

        ULONG id = session::invalid;
        if (!NT_SUCCESS(IoGetRequestorSessionId(irp, &id))) {
                NT_ASSERT(id == session::invalid);
        }
        return id;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::sid_from_hex(_Out_ sid_data &sid, _In_ const UNICODE_STRING &hex)
{
        PAGED_CODE();
        sid = {};

        if (empty(hex)) {
                return STATUS_INVALID_PARAMETER;
        }

        ULONG cch = hex.Length/sizeof(*hex.Buffer);
        if (cch % 2) {
                return STATUS_INVALID_PARAMETER;
        }

        auto cb = cch/2;
        if (!check_sid_length_range(cb)) {
                return STATUS_INVALID_PARAMETER;
        }

        for (ULONG i = 0; i < cb; ++i) {
                auto hi = from_hex(hex.Buffer[2*i]);
                auto lo = from_hex(hex.Buffer[2*i + 1]);
                if (hi < 0 || lo < 0) {
                        return STATUS_INVALID_PARAMETER;
                }
                sid.data[i] = static_cast<UCHAR>((hi << 4) | lo);
        }

        sid.length = cb;

        if (is_valid_sid(sid)) {
                return STATUS_SUCCESS;
        }

        sid = {};
        return STATUS_INVALID_SID;
}
