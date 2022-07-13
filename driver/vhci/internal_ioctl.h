#pragma once

#include <wdm.h>

struct vpdo_dev_t;

_IRQL_requires_max_(DISPATCH_LEVEL)
void send_cmd_unlink(vpdo_dev_t &vpdo, IRP *irp);

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_INTERNAL_DEVICE_CONTROL)
extern "C" NTSTATUS vhci_internal_ioctl(__in DEVICE_OBJECT *devobj, __in IRP *irp);