#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS vhci_ioctl_vhub(pvhub_dev_t vhub, PIRP irp, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG *poutlen);

#ifdef __cplusplus
}
#endif

