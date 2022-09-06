#pragma once

#include <libdrv\pageable.h>
#include <wdm.h>

_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
extern "C" PAGEABLE NTSTATUS vhci_add_device(_In_ DRIVER_OBJECT *DriverObject, _In_ DEVICE_OBJECT *PhysicalDeviceObject);
