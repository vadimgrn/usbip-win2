#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS vhub_get_roothub_name(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen);
PAGEABLE NTSTATUS vhci_ioctl_vhci(pvhci_dev_t vhci, PIO_STACK_LOCATION irpstack, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG *poutlen);

#ifdef __cplusplus
}
#endif

