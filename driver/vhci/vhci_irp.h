#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include <wdm.h>

PAGEABLE NTSTATUS irp_pass_down(PDEVICE_OBJECT devobj, PIRP irp);
PAGEABLE NTSTATUS irp_send_synchronously(PDEVICE_OBJECT devobj, PIRP irp);

NTSTATUS irp_done(IRP *irp, NTSTATUS status);

__inline NTSTATUS irp_done_success(IRP *irp)
{
	return irp_done(irp, STATUS_SUCCESS);
}

__inline NTSTATUS irp_done_iostatus(IRP *irp)
{
	return irp_done(irp, irp->IoStatus.Status);
}

#ifdef __cplusplus
}
#endif
