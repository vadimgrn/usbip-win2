#pragma once

#include "pageable.h"
#include "dev.h"

PAGEABLE NTSTATUS vhub_get_roothub_name(vhub_dev_t *vhub, void *buffer, ULONG inlen, ULONG *poutlen);
PAGEABLE NTSTATUS vhci_ioctl_vhci(vhci_dev_t *vhci, IO_STACK_LOCATION *irpstack, ULONG ioctl_code, void *buffer, ULONG inlen, ULONG *poutlen);
