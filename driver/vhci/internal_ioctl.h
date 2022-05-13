#pragma once

#include <wdm.h>

struct vpdo_dev_t;

NTSTATUS send_to_server(vpdo_dev_t*, IRP*);
NTSTATUS send_cmd_unlink(vpdo_dev_t*, IRP*);
