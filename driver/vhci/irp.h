#pragma once

#include "pageable.h"
#include <wdm.h>

PAGEABLE NTSTATUS irp_pass_down(DEVICE_OBJECT *devobj, IRP *irp);
PAGEABLE NTSTATUS irp_send_synchronously(DEVICE_OBJECT *devobj, IRP *irp);

NTSTATUS irp_done(IRP *irp, NTSTATUS status);
void complete_canceled_irp(IRP *irp);

inline auto irp_done_success(IRP *irp)
{
	return irp_done(irp, STATUS_SUCCESS);
}

inline auto irp_done_iostatus(IRP *irp)
{
	return irp_done(irp, irp->IoStatus.Status);
}
