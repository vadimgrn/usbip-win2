#pragma once

#include "pageable.h"
#include <wdm.h>

struct vhci_dev_t;

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS reg_wmi(vhci_dev_t *vhci);

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS dereg_wmi(vhci_dev_t *vhci);

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_SYSTEM_CONTROL)
extern "C" PAGEABLE NTSTATUS vhci_system_control(_In_ PDEVICE_OBJECT devobj, _In_ PIRP irp);
