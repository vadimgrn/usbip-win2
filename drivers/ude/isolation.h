/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/wdf_cpp.h>

struct _DEVICE_OBJECT;
using DEVICE_OBJECT = struct _DEVICE_OBJECT;

struct _UNICODE_STRING;
using UNICODE_STRING = struct _UNICODE_STRING;

namespace usbip
{
        struct device_attributes;
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
 * Validates and initializes device isolation ownership and captures caller user/logon SIDs
 * for volume DACL hardening.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS init_requestor_isolation(_In_ WDFREQUEST request, _Inout_ device_attributes &attr);

/*
 * Captures the USB PDO instance ID and container ID for later disk/volume ownership checks.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void save_pdo_properties(_Inout_ device_attributes &attr, _In_ DEVICE_OBJECT *pdo);

/*
 * Removes \GLOBAL??\<drive_letter>: and hardens the volume and root directory DACLs in kernel mode
 * after verifying the volume belongs to the device.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS isolate_device_volume(
        _In_ WDFREQUEST request,
        _In_ const device_ctx &dev,
        _In_ const UNICODE_STRING &nt_device,
        _In_ wchar_t drive_letter);

/*
 * Registers PnP volume interface arrival notification to harden isolated volume DACLs
 * as soon as volumes arrive.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS register_volume_notification(_In_ WDFDEVICE vhci);

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

