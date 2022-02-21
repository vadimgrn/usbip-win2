#pragma once

#include <wdm.h>

struct vpdo_dev_t;

void send_cmd_unlink(vpdo_dev_t *vpdo, IRP *irp);
NTSTATUS send_to_server(vpdo_dev_t *vpdo, IRP *irp, bool clear_ctx = true);
