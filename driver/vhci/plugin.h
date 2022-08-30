#pragma once

#include "pageable.h"
#include <ntdef.h>

struct vhci_dev_t;
struct vhub_dev_t;
struct ioctl_usbip_vhci_plugin;

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_When_(return>=0, _Kernel_clear_do_init_(yes))
PAGEABLE NTSTATUS vhci_plugin_vpdo(vhci_dev_t *vhci, ioctl_usbip_vhci_plugin &r);

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS vhci_unplug_vpdo(vhub_dev_t *vhub, int port);

