#pragma once

#include <wdm.h>

struct vpdo_dev_t;

NTSTATUS handle_irp(vpdo_dev_t *vpdo, IRP *urb_irp);
NTSTATUS complete_internal_ioctl(IRP *irp, NTSTATUS status);
