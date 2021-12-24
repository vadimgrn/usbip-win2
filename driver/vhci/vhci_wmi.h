#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS vhci_system_control(__in PDEVICE_OBJECT devobj, __in PIRP irp);
PAGEABLE NTSTATUS reg_wmi(pvhci_dev_t vhci);
PAGEABLE NTSTATUS dereg_wmi(pvhci_dev_t vhci);

#ifdef __cplusplus
}
#endif
