#pragma once

#include <wdm.h>
#include "pageable.h"

struct _IRP;
struct vpdo_dev_t;
struct _USB_DESCRIPTOR_REQUEST;

void send_cmd_unlink(vpdo_dev_t &vpdo, _IRP *irp);
PAGEABLE NTSTATUS get_descriptor_from_node_connection(vpdo_dev_t &vpdo, IRP *irp, _USB_DESCRIPTOR_REQUEST &r);
