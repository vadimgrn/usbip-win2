/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "codeseg.h"

struct _IRP;
using IRP = struct _IRP;

struct _UNICODE_STRING;
using UNICODE_STRING = struct _UNICODE_STRING;

using HANDLE = void*;

namespace usbip
{

struct sid_blob;

/*
 * Compares two sid_blob structures for binary SID equality using RtlEqualSid.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool operator==(_In_ const sid_blob &a, _In_ const sid_blob &b);

} // namespace usbip


namespace libdrv
{

using usbip::sid_blob;
using usbip::operator==;

/*
 * Retrieves the effective User SID of the caller who issued the IRP.
 * Inspects thread impersonation token first, falling back to process primary token.
 *
 * @param irp caller request
 * @param result out buffer for SID bytes and length
 * @return STATUS_SUCCESS on success, STATUS_NO_TOKEN if no token is available,
 *         or another NTSTATUS failure code.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS get_requestor_sid(_In_ IRP *irp, _Out_ sid_blob &result);

/*
 * Retrieves the logon session SID (SE_GROUP_LOGON_ID) of the caller who issued the IRP.
 *
 * @param irp caller request
 * @param result out buffer for SID bytes and length
 * @return STATUS_SUCCESS on success, STATUS_NOT_FOUND if the token has no logon SID,
 *         STATUS_NO_TOKEN if no token is available, or another NTSTATUS failure code.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS get_requestor_logon_sid(_In_ IRP *irp, _Out_ sid_blob &result);

/*
 * The Terminal Server session that issued the request, or session::invalid if it cannot be
 * determined (e.g. a kernel-mode request), which denies ownership by design.
 * @return session id or session::invalid
 */
_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED ULONG get_requestor_session_id(_In_ IRP *irp);

/*
 * Checks whether the caller of the IRP matches the expected SID.
 * KernelMode callers always evaluate to true.
 *
 * @param irp caller request
 * @param expected_sid target SID to match against
 * @return true if KernelMode or caller SID equals expected_sid
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_caller_sid(_In_ IRP *irp, _In_ const sid_blob &expected_sid);

/*
 * Checks whether the caller of the IRP is NT AUTHORITY\SYSTEM (or KernelMode).
 *
 * @param irp caller request
 * @return true if KernelMode or caller SID equals SeLocalSystemSid
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_system_request(_In_ IRP *irp);

/*
 * Checks whether the caller of the IRP has administrative privileges (or is KernelMode).
 *
 * @param irp caller request
 * @return true if KernelMode or caller token has admin privilege
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_admin_request(_In_ IRP *irp);

/*
 * Checks whether the sid_blob structure contains a valid SID.
 *
 * @param sid target SID structure to validate
 * @return true if sid length matches RtlLengthSid and RtlValidSid succeeds
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool is_valid(_In_ const sid_blob &sid);

/*
 * Checks whether the specified SID is NT AUTHORITY\SYSTEM (SeLocalSystemSid).
 *
 * @param sid target SID to check
 * @return true if sid equals SeLocalSystemSid
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool is_system_sid(_In_ const sid_blob &sid);

/*
 * Parses a hexadecimal string representation of a binary SID into a sid_blob structure.
 *
 * @param sid output buffer
 * @param hex unicode string containing hex-encoded binary SID
 * @return STATUS_SUCCESS on success, STATUS_INVALID_PARAMETER if malformed,
 *         or STATUS_INVALID_SID if binary SID validation fails.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS sid_from_hex(_Out_ sid_blob &sid, _In_ const UNICODE_STRING &hex);

/*
 * Sets a protected DACL on a kernel object or directory handle granting GENERIC_ALL
 * with inheritance (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE) to SYSTEM, user_sid,
 * and (if valid) logon_sid. Administrators (BA) is omitted to deny foreign admins.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS set_volume_dacl(
        _In_ HANDLE handle, _In_ const sid_blob &user_sid, _In_ const sid_blob &logon_sid);

/*
 * Removes \GLOBAL??\<drive_letter>: if and only if its target matches expected_nt_device.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS remove_global_drive_letter(
        _In_ wchar_t drive_letter, _In_ const UNICODE_STRING &expected_nt_device);

} // namespace libdrv
