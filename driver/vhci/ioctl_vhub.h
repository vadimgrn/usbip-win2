#pragma once

#include "pageable.h"
#include <ntdef.h>

struct vhub_dev_t;

PAGEABLE NTSTATUS vhci_ioctl_vhub(_Inout_ vhub_dev_t &vhub, _In_ ULONG ioctl_code, _Inout_ PVOID buffer, _In_ ULONG inlen, _Inout_ ULONG &outlen);
