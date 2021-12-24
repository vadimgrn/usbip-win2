#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS vhci_ioctl_user_request(pvhci_dev_t vhci, PVOID buffer, ULONG inlen, PULONG poutlen);

#ifdef __cplusplus
}
#endif
