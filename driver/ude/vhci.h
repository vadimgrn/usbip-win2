/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

#include <initguid.h>
#include <usbip\vhci.h>

#include <libdrv\pageable.h>

namespace usbip
{

struct vhci_context
{
        WDFQUEUE default_queue;

        UDECXUSBDEVICE devices[vhci::TOTAL_PORTS];
        WDFSPINLOCK devices_lock;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(vhci_context, get_vhci_context)

struct request_context
{
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(request_context, get_request_context)

_Function_class_(EVT_WDF_DRIVER_DEVICE_ADD)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS DriverDeviceAdd(_In_ WDFDRIVER, _Inout_ WDFDEVICE_INIT *DeviceInit);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE int assign_hub_port(_In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE udev, _In_ UDECX_USB_DEVICE_SPEED speed);

constexpr auto to_hci_version(_In_ UDECX_USB_DEVICE_SPEED speed)
{
        return speed >= UdecxUsbSuperSpeed ? vhci::HCI_USB3 : vhci::HCI_USB2;
}

} // namespace usbip
