#pragma once

struct _IRP;
struct vpdo_dev_t;

void send_cmd_unlink(vpdo_dev_t &vpdo, _IRP *irp);
