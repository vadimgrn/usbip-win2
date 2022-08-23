#pragma once

#include "pageable.h"
#include <wdm.h>

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
extern "C" PAGEABLE NTSTATUS vhci_ioctl(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);
