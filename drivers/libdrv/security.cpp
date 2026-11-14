/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <ntifs.h>

#include "security.h"
#include "handle.h"
#include "strconv.h"

#include <usbip/hex.h>
#include <usbip/vhci.h>

#include <libusbip/generic_handle_ex.h>

using namespace usbip;

namespace
{

/*
 * @see libusbip/src/sid.cpp
 */
constexpr auto check_sid_length_range(_In_ ULONG bytes)
{
        static_assert(sizeof(sid_blob::data) == SECURITY_MAX_SID_SIZE);

        return  bytes >= offsetof(SID, SubAuthority) &&
                bytes <= sizeof(sid_blob::data);
}

constexpr auto is_drive_letter(_In_ wchar_t ch)
{
        return  (ch >= L'A' && ch <= L'Z') ||
                (ch >= L'a' && ch <= L'z');
}

constexpr auto max_acl_size(_In_ ULONG ace_cnt)
{
        constexpr auto max_ace_size = offsetof(ACCESS_ALLOWED_ACE, SidStart) + SECURITY_MAX_SID_SIZE;
        return sizeof(ACL) + ace_cnt*max_ace_size;
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

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED inline auto add_ace(_Inout_ ACL *acl, _In_ PSID sid)
{
        PAGED_CODE();
        constexpr ULONG ace_flags = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
        return RtlAddAccessAllowedAceEx(acl, ACL_REVISION, ace_flags, GENERIC_ALL, sid);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto init_volume_acl(
        _Out_writes_bytes_(acl_size) ACL *acl, _In_ ULONG acl_size,
        _In_ const sid_blob &user_sid, _In_ const sid_blob &logon_sid)
{
        PAGED_CODE();

        auto st = RtlCreateAcl(acl, acl_size, ACL_REVISION);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        st = add_ace(acl, SeExports->SeLocalSystemSid);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        if (!libdrv::is_system_sid(user_sid)) {
                st = add_ace(acl, const_cast<UCHAR*>(user_sid.data));
                if (!NT_SUCCESS(st)) {
                        return st;
                }
        }

        if (libdrv::is_valid(logon_sid) && logon_sid != user_sid && !libdrv::is_system_sid(logon_sid)) {
                st = add_ace(acl, const_cast<UCHAR*>(logon_sid.data));
                if (!NT_SUCCESS(st)) {
                        return st;
                }
        }

        return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::get_requestor_sid(_In_ IRP *irp, _Out_ sid_blob &result)
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
        libdrv::paged_pool_handle<TOKEN_USER> del(tu);

        auto sid = tu->User.Sid;

        if (!(sid && RtlValidSid(sid))) {
                return STATUS_INVALID_SID;
        }

        st = RtlCopySid(sizeof(result.data), result.data, sid);
        if (NT_SUCCESS(st)) {
                result.length = RtlLengthSid(result.data);
                NT_ASSERT(is_valid(result));
        }
        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::get_requestor_logon_sid(_In_ IRP *irp, _Out_ sid_blob &result)
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

        TOKEN_GROUPS *tg{};
        auto st = SeQueryInformationToken(token.get(), TokenGroups, reinterpret_cast<PVOID*>(&tg));
        if (!NT_SUCCESS(st)) {
                return st;
        }
        libdrv::paged_pool_handle<TOKEN_GROUPS> del(tg);

        for (ULONG i = 0; i < tg->GroupCount; ++i) {

                const auto &g = tg->Groups[i];

                if ((g.Attributes & se_group_logon_id) != se_group_logon_id) {
                        continue;
                }

                if (!(g.Sid && RtlValidSid(g.Sid))) {
                        return STATUS_INVALID_SID;
                }

                st = RtlCopySid(sizeof(result.data), result.data, g.Sid);
                if (NT_SUCCESS(st)) {
                        result.length = RtlLengthSid(result.data);
                        NT_ASSERT(is_valid(result));
                }
                return st;
        }

        return STATUS_NOT_FOUND;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool libdrv::is_caller_sid(_In_ IRP *irp, _In_ const sid_blob &expected_sid)
{
        PAGED_CODE();

        if (!irp) {
                return false;
        }

        if (irp->RequestorMode == KernelMode) {
                return true;
        }

        sid_blob caller_sid{};

        return  NT_SUCCESS(get_requestor_sid(irp, caller_sid)) &&
                caller_sid == expected_sid;
}

/*
 * @aee libusbip/src/sid.h, is_valid
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool libdrv::is_valid(_In_ const sid_blob &sid)
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
bool libdrv::is_system_sid(_In_ const sid_blob &sid)
{
        NT_ASSERT(SeExports);
        NT_ASSERT(SeExports->SeLocalSystemSid);

        return  is_valid(sid) &&
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

        sid_blob caller_sid;

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
bool usbip::operator==(_In_ const sid_blob &a, _In_ const sid_blob &b)
{
        return  a.length == b.length &&
                libdrv::is_valid(a) &&
                libdrv::is_valid(b) &&
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

        auto id = session::invalid;
        if (!NT_SUCCESS(IoGetRequestorSessionId(irp, &id))) {
                NT_ASSERT(id == session::invalid);
        }
        return id;
}

/*
 * @see libusbip, parse_sid_hex
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::sid_from_hex(_Out_ sid_blob &sid, _In_ const UNICODE_STRING &hex)
{
        PAGED_CODE();
        sid = {};

        if (empty(hex)) {
                return STATUS_INVALID_PARAMETER;
        }

        ULONG cch = length_cch(hex);
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

        if (!is_valid(sid)) {
                sid = {};
                return STATUS_INVALID_SID;
        }

        return STATUS_SUCCESS;
}

/*
 * ACL itself has alignof(ACL) == 2 (USHORT fields only), but RtlCreateAcl and
 * ACCESS_ALLOWED_ACE require 4-byte (ULONG) alignment.
 *
 * Zero-initialize acl_buf: RtlCreateAcl sets AclSize = sizeof(acl_buf) without
 * clearing the buffer, and ZwSetSecurityObject copies all AclSize bytes (including
 * unused trailing slack space when SIDs are shorter than SECURITY_MAX_SID_SIZE),
 * which would otherwise leak uninitialized kernel stack memory.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::set_volume_dacl(
        _In_ HANDLE handle, _In_ const sid_blob &user_sid, _In_ const sid_blob &logon_sid)
{
        PAGED_CODE();

        if (!(handle && is_valid(user_sid))) {
                return STATUS_INVALID_PARAMETER;
        }

        alignas(ULONG) UCHAR acl_buf[max_acl_size(3)]{}; // SYSTEM, user_sid, logon_sid
        auto acl = reinterpret_cast<ACL*>(acl_buf);

        auto st = init_volume_acl(acl, sizeof(acl_buf), user_sid, logon_sid);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        SECURITY_DESCRIPTOR sd;
        st = RtlCreateSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        st = RtlSetDaclSecurityDescriptor(&sd, true, acl, false);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        sd.Control |= SE_DACL_PROTECTED;
        return ZwSetSecurityObject(handle, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &sd);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS libdrv::remove_global_drive_letter(
        _In_ wchar_t drive_letter, _In_ const UNICODE_STRING &expected_nt_device)
{
        PAGED_CODE();

        if (!is_drive_letter(drive_letter) || empty(expected_nt_device)) {
                return STATUS_INVALID_PARAMETER;
        }

        wchar_t link_buf[] { L'\\', L'G', L'L', L'O', L'B', L'A', L'L', L'?', L'?', L'\\', drive_letter, L':' };
        UNICODE_STRING link_name {
                .Length = sizeof(link_buf),
                .MaximumLength = sizeof(link_buf),
                .Buffer = link_buf
        };

        OBJECT_ATTRIBUTES attr;
        InitializeObjectAttributes(&attr, &link_name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);

        HANDLE hlink;
        auto st = ZwOpenSymbolicLinkObject(&hlink, SYMBOLIC_LINK_QUERY | DELETE, &attr);
        if (!NT_SUCCESS(st)) {
                return st;
        }
        libdrv::zw_handle del(hlink);

        wchar_t target_buf[MAX_PATH];
        UNICODE_STRING target {
                .MaximumLength = sizeof(target_buf),
                .Buffer = target_buf
        };

        st = ZwQuerySymbolicLinkObject(hlink, &target, nullptr);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        if (!RtlEqualUnicodeString(&target, &expected_nt_device, true)) {
                return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        return ZwMakeTemporaryObject(hlink);
}
