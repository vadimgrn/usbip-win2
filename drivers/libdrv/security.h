/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "codeseg.h"

struct _IRP;
using IRP = struct _IRP;

struct _UNICODE_STRING;
using UNICODE_STRING = struct _UNICODE_STRING;

namespace usbip::vhci
{
        struct sid_data;
}

namespace libdrv
{

using usbip::vhci::sid_data;

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
PAGED NTSTATUS get_requestor_sid(_In_ IRP *irp, _Out_ sid_data &result);

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
PAGED bool is_caller_sid(_In_ IRP *irp, _In_ const sid_data &expected_sid);

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
 * Checks whether the sid_data structure contains a valid SID.
 *
 * @param sid target SID structure to validate
 * @return true if sid length matches RtlLengthSid and RtlValidSid succeeds
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool is_valid_sid(_In_ const sid_data &sid);

/*
 * Checks whether the specified SID is NT AUTHORITY\SYSTEM (SeLocalSystemSid).
 *
 * @param sid target SID to check
 * @return true if sid equals SeLocalSystemSid
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool is_system_sid(_In_ const sid_data &sid);

/*
 * Compares two sid_data structures for binary SID equality using RtlEqualSid.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool equal_sid(_In_ const sid_data &a, _In_ const sid_data &b);

/*
 * Parses a hexadecimal string representation of a binary SID into a sid_data structure.
 *
 * @param sid output buffer
 * @param hex unicode string containing hex-encoded binary SID
 * @return STATUS_SUCCESS on success, STATUS_INVALID_PARAMETER if malformed,
 *         or STATUS_INVALID_SID if binary SID validation fails.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS sid_from_hex(_Out_ sid_data &sid, _In_ const UNICODE_STRING &hex);

} // namespace libdrv

namespace usbip::vhci
{
        using libdrv::is_valid_sid;
}
