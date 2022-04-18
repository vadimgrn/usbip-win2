#pragma once

#include "dev.h"
#include <wdm.h>

NTSTATUS complete_internal_ioctl(IRP *irp, NTSTATUS status);

NTSTATUS send_to_server(vpdo_dev_t*, IRP*);
NTSTATUS send_cmd_unlink(vpdo_dev_t*, IRP*);
