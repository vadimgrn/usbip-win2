/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "codeseg.h"
#include <usbip/vhci.h>

struct _IRP;
typedef struct _IRP IRP;

namespace libdrv
{

using usbip::vhci::sid_data;

/*
 * Retrieves the effective User SID of the caller who issued the IRP.
 * Inspects thread impersonation token first, falling back to process primary token.
 *
 * @param irp caller request
 * @param sid out buffer for SID bytes and length
 * @return STATUS_SUCCESS on success, STATUS_NO_TOKEN if no token is available,
 *         or another NTSTATUS failure code.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS get_requestor_sid(_In_ IRP *irp, _Out_ sid_data &sid);

/*
 * The Terminal Server session that issued the request, or invalid_session_id if it cannot be
 * determined (e.g. a kernel-mode request), which denies ownership by design.
 * @return session id or invalid_session_id
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

} // namespace libdrv
