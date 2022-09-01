#pragma once

#include "pageable.h"
#include <ntdef.h>

struct vhub_dev_t;
struct ioctl_usbip_vhci_plugin;

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_When_(return>=0, _Kernel_clear_do_init_(yes))
PAGEABLE NTSTATUS plugin_vpdo(vhub_dev_t &vhub, ioctl_usbip_vhci_plugin &r);

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS unplug_vpdo(vhub_dev_t &vhub, int port);
