#pragma once

#include <ntdef.h>

struct _IRP;
struct vpdo_dev_t;

void send_cmd_unlink(vpdo_dev_t &vpdo, _IRP *irp);

NTSTATUS init_lookaside_list();
void delete_lookaside_list();
