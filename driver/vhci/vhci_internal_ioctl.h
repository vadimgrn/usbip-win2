#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "vhci_dev.h"

NTSTATUS vhci_internal_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP Irp);

#ifdef __cplusplus
}
#endif
