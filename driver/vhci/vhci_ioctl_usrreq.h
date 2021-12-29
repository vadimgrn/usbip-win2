#pragma once

#include "pageable.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS vhci_ioctl_user_request(vhci_dev_t * vhci, PVOID buffer, ULONG inlen, PULONG poutlen);
