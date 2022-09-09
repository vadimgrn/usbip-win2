#pragma once

#include <wdm.h>
#include <wdf.h>

#include <libdrv\pageable.h>

struct device_context
{
    WDFUSBDEVICE UsbDevice;
    ULONG PrivateDeviceData;  // just a placeholder
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(device_context, DeviceGetContext)

_Function_class_(EVT_WDF_DRIVER_DEVICE_ADD)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS DriverDeviceAdd(_In_ WDFDRIVER, _Inout_ WDFDEVICE_INIT *DeviceInit);

