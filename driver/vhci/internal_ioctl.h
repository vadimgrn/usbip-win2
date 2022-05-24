#pragma once

#include <wdm.h>

struct _IRP;
struct vpdo_dev_t;

void send_cmd_unlink(vpdo_dev_t &vpdo, _IRP *irp);

extern LOOKASIDE_LIST_EX send_context_list;
NTSTATUS init_send_context_list();
