#pragma once

#include <libdrv\pageable.h>
#include <ntdef.h>

struct vhci_dev_t;
struct _USBUSER_REQUEST_HEADER;

PAGEABLE NTSTATUS vhci_ioctl_user_request(vhci_dev_t &vhci, _USBUSER_REQUEST_HEADER *hdr, ULONG &outlen);
