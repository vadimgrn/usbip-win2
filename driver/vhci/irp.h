#pragma once

#include "pageable.h"
#include <wdm.h>

PAGEABLE NTSTATUS irp_pass_down(DEVICE_OBJECT *devobj, IRP *irp);
PAGEABLE NTSTATUS irp_send_synchronously(DEVICE_OBJECT *devobj, IRP *irp);

NTSTATUS CompleteRequest(IRP *irp, NTSTATUS status = STATUS_SUCCESS);

inline auto CompleteRequestIoStatus(IRP *irp)
{
	return CompleteRequest(irp, irp->IoStatus.Status);
}

struct vpdo_dev_t;
void complete_canceled_irp(vpdo_dev_t *vpdo, IRP *irp);
