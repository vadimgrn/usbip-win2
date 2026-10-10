/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/wdf_cpp.h>

namespace usbip
{
        struct device_ctx;
        struct device_attributes;
}

namespace usbip::session
{

/*
 * Captures the USB PDO instance ID and container ID for later disk/volume ownership checks.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void save_pdo_properties(_Inout_ device_attributes &attr, _In_opt_ DEVICE_OBJECT *pdo);

/*
 * Removes \GLOBAL??\<drive_letter>: and hardens the volume and disk DACLs in kernel mode
 * after verifying the volume belongs to the device.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS isolate_device_volume(
        _In_ WDFREQUEST request,
        _Inout_ device_ctx &dev,
        _In_ const UNICODE_STRING &nt_device,
        _Inout_ wchar_t &drive_letter);

/*
 * Registers PnP volume interface arrival notification to harden isolated volume DACLs
 * as soon as volumes arrive.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS register_volume_notification(_In_ WDFDEVICE vhci);

/*
 * Unregisters the PnP volume notification and waits for active callbacks and work items to finish.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void unregister_volume_notification(_In_ WDFDEVICE vhci);

} // namespace usbip::session
