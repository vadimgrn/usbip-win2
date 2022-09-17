/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#include <libdrv\pageable.h>

struct vhci_context
{
        WDFQUEUE default_queue;
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

