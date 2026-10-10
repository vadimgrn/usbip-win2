/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/wdf_cpp.h>

namespace usbip
{
        struct device_ctx;
        struct fileobject_ctx;
}

namespace usbip::vhci
{
        struct device_owner;
}

namespace usbip::session
{

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED ULONG get_requestor_session_id(_In_ WDFREQUEST request);

/*
 * Checks whether the requestor has administrative privileges (or is kernel mode).
 * Safely inspects the caller thread's impersonation token, falling back to the process primary token.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_admin_request(_In_ WDFREQUEST request);

/*
 * Retrieves the effective owner (session ID and User SID) of the requestor.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS get_requestor_owner(_In_ WDFREQUEST request, _Out_ vhci::device_owner &owner);

/*
 * Checks whether the owner owns the device under its active isolation mode.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_owner(_In_ const device_ctx &dev, _In_ const vhci::device_owner &owner);

/*
 * Checks whether the requestor is denied access to the device based on session and user isolation.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_access_denied(
        _In_ const device_ctx &dev, _In_ const vhci::device_owner &owner);

/*
 * Checks whether the subscriber is denied access to the device based on session and user isolation.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_access_denied(_In_ const device_ctx &dev, _In_ const fileobject_ctx &fobj);

/*
 * Checks whether the subscriber is denied access to events from the device owner.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_access_denied(
        _In_ const vhci::device_owner &owner, _In_ const fileobject_ctx &fobj);

/*
 * Checks whether the requestor is denied access to read from the file object.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_access_denied(_In_ const fileobject_ctx &fobj, _In_ WDFREQUEST request);

} // namespace usbip::session

