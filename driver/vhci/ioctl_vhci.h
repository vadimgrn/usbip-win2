#pragma once

#include "pageable.h"
#include "dev.h"

PAGEABLE NTSTATUS get_hcd_driverkey_name(vhci_dev_t *vhci, USB_HCD_DRIVERKEY_NAME &r, ULONG &outlen);
PAGEABLE NTSTATUS vhub_get_roothub_name(vhub_dev_t *vhub, USB_ROOT_HUB_NAME &r, ULONG &outlen);
PAGEABLE NTSTATUS vhci_ioctl_vhci(vhci_dev_t *vhci, ULONG ioctl_code, void *buffer, ULONG inlen, ULONG &outlen);
