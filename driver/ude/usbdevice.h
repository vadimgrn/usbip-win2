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
        int port; // vhci_context.devices[port - 1]
};        
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(usbdevice_context, get_usbdevice_context)

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS create_usbdevice(
        _Out_ UDECXUSBDEVICE &udev, _In_ WDFDEVICE vhci, _In_ UDECX_USB_DEVICE_SPEED speed);

} // namespace usbip
