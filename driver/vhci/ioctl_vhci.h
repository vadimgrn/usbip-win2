#pragma once

#include "pageable.h"
#include "dev.h"

PAGEABLE NTSTATUS get_hcd_driverkey_name(_In_ vhci_dev_t *vhci, _Out_ USB_HCD_DRIVERKEY_NAME &r, _Out_ ULONG &outlen);
PAGEABLE NTSTATUS get_roothub_name(_In_ vhub_dev_t *vhub, _Out_ USB_ROOT_HUB_NAME &r, _Out_ ULONG &outlen);
PAGEABLE NTSTATUS vhci_ioctl_vhci(_In_ vhci_dev_t *vhci, _In_ ULONG ioctl_code, void *buffer, _In_ ULONG inlen, _Out_ ULONG &outlen);
