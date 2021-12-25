#pragma once

#include "basetype.h"
#include <wdm.h>

PAGEABLE NTSTATUS irp_pass_down(PDEVICE_OBJECT devobj, PIRP irp);
PAGEABLE NTSTATUS irp_send_synchronously(PDEVICE_OBJECT devobj, PIRP irp);

NTSTATUS irp_done(IRP *irp, NTSTATUS status);

inline auto irp_done_success(IRP *irp)
{
	return irp_done(irp, STATUS_SUCCESS);
}

inline auto irp_done_iostatus(IRP *irp)
{
	return irp_done(irp, irp->IoStatus.Status);
}
