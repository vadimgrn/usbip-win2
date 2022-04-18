#pragma once

#include "pageable.h"
#include "dev.h"
#include "usbip_vhci_api.h"

PAGEABLE NTSTATUS vhci_plugin_vpdo(vhci_dev_t *vhci, ioctl_usbip_vhci_plugin &pi, ULONG inlen, ULONG &);
PAGEABLE NTSTATUS vhci_unplug_vpdo(vhci_dev_t *vhci, int port);

