#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS vhci_ioctl_vhub(pvhub_dev_t vhub, PIRP irp, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG *poutlen);
