#pragma once

#include "pageable.h"
#include "dev.h"

#include <usbuser.h>

PAGEABLE NTSTATUS vhci_ioctl_user_request(vhci_dev_t * vhci, USBUSER_REQUEST_HEADER *hdr, ULONG &outlen);
