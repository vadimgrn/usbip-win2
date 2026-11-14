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
        struct isolation_data;
}

namespace usbip::session
{

/*
 * Checks whether the requestor has administrative privileges (or is kernel mode).
 * Safely inspects the caller thread's impersonation token, falling back to the process primary token.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool is_admin_request(_In_ WDFREQUEST request);

/*
 * Validates and initializes device isolation ownership from the attach request.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS init_requestor_isolation(_In_ WDFREQUEST request, _Inout_ vhci::isolation_data &iso);

/*
 * Resolves caller ownership and privilege.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS get_caller_owner(
        _In_ WDFREQUEST request, _Out_ bool &is_admin, _Out_ vhci::device_owner &owner);

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

