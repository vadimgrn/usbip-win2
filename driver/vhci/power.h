#pragma once

#include <wdm.h>

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_POWER)
extern "C" NTSTATUS vhci_power(__in PDEVICE_OBJECT devobj, __in PIRP irp);
