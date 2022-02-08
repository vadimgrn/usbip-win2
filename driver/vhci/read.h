#pragma once

#include <wdm.h>

struct vpdo_dev_t;
NTSTATUS do_read(vpdo_dev_t *vpdo, IRP *read_irp, IRP *urb_irp);