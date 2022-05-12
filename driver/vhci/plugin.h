#pragma once

#include "pageable.h"
#include <ntdef.h>

struct vhci_dev_t;
struct ioctl_usbip_vhci_plugin;

PAGEABLE NTSTATUS vhci_plugin_vpdo(vhci_dev_t *vhci, ioctl_usbip_vhci_plugin &r);
PAGEABLE NTSTATUS vhci_unplug_vpdo(vhci_dev_t *vhci, int port);

