#pragma once

#include <wdm.h>

struct vpdo_dev_t;

NTSTATUS submit_to_server(vpdo_dev_t *vpdo, IRP *irp);
NTSTATUS complete_internal_ioctl(IRP *irp, NTSTATUS status);
