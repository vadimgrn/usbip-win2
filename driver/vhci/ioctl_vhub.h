#pragma once

#include "pageable.h"
#include "dev.h"

PAGEABLE NTSTATUS vhci_ioctl_vhub(vhub_dev_t *vhub, IRP *irp, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG &outlen);
