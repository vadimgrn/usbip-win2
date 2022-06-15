#pragma once

#include "pageable.h"
#include <ntdef.h>

struct _IRP;
struct _USB_DESCRIPTOR_REQUEST;
struct vpdo_dev_t;


void send_cmd_unlink(vpdo_dev_t &vpdo, _IRP *irp);
PAGEABLE NTSTATUS get_descriptor_from_node_connection(vpdo_dev_t &vpdo, _IRP *irp, _USB_DESCRIPTOR_REQUEST &r, ULONG TransferBufferLength);
