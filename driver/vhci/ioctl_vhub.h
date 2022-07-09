#pragma once

#include "pageable.h"
#include <ntdef.h>

struct vhub_dev_t;

PAGEABLE NTSTATUS vhci_ioctl_vhub(vhub_dev_t *vhub, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG &outlen);
