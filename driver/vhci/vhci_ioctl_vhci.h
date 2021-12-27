#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS vhub_get_roothub_name(vhub_dev_t * vhub, PVOID buffer, ULONG inlen, PULONG poutlen);
PAGEABLE NTSTATUS vhci_ioctl_vhci(vhci_dev_t * vhci, PIO_STACK_LOCATION irpstack, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG *poutlen);
