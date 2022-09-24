/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <libdrv\pageable.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

namespace usbip
{

struct usbdevice_context
{
        // from vhci::ioctl_plugin
        PSTR busid;
        UNICODE_STRING node_name;
        UNICODE_STRING service_name;
        UNICODE_STRING serial; // user-defined
        //

        WDFDEVICE vhci;
        int port; // vhci_context.devices[port - 1]
        bool destroyed;
};        
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(usbdevice_context, get_usbdevice_context)

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS create_usbdevice(
        _Out_ UDECXUSBDEVICE &udev, _In_ WDFDEVICE vhci, _In_ UDECX_USB_DEVICE_SPEED speed);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void destroy_usbdevice(_In_ UDECXUSBDEVICE udev);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS schedule_destroy_usbdevice(_In_ UDECXUSBDEVICE udev);

} // namespace usbip
