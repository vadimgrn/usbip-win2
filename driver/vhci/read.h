#pragma once

#include <wdm.h>

struct vpdo_dev_t;
NTSTATUS do_read(IRP *read_irp, IRP *urb_irp, bool complete);