/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

#include <libdrv\pageable.h>
#include <libdrv\wdfobjectref.h>

#include <initguid.h>
#include <usbip\vhci.h>

namespace usbip
{

struct vhci_context
{
        WDFQUEUE queue;

        UDECXUSBDEVICE devices[vhci::TOTAL_PORTS]; // do not access directly, functions must be used
        WDFSPINLOCK devices_lock;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(vhci_context, get_vhci_context)

struct request_context
{
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(request_context, get_request_context)

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE int remember_usbdevice(_In_ UDECXUSBDEVICE udev);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
wdf::WdfObjectRef get_usbdevice(_In_ WDFDEVICE vhci, _In_ int port);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void forget_usbdevice(_In_ UDECXUSBDEVICE udev);

_Function_class_(EVT_WDF_DRIVER_DEVICE_ADD)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS DriverDeviceAdd(_In_ WDFDRIVER, _Inout_ WDFDEVICE_INIT *DeviceInit);

_IRQL_requires_max_(DISPATCH_LEVEL)
inline void WdfObjectDeleteSafe(_In_ WDFOBJECT Object)
{
        if (Object) {
                WdfObjectDelete(Object);
        }
}

} // namespace usbip
