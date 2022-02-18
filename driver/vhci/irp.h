#pragma once

#include "pageable.h"
#include <wdm.h>

struct vpdo_dev_t;

PAGEABLE NTSTATUS irp_pass_down(DEVICE_OBJECT *devobj, IRP *irp);
PAGEABLE NTSTATUS irp_send_synchronously(DEVICE_OBJECT *devobj, IRP *irp);

NTSTATUS CompleteRequest(IRP *irp, NTSTATUS status = STATUS_SUCCESS);

void complete_canceled_irp(vpdo_dev_t *vpdo, IRP *irp);
void irp_canceled(vpdo_dev_t *vpdo, IRP *irp);

inline auto CompleteRequestIoStatus(IRP *irp)
{
	return CompleteRequest(irp, irp->IoStatus.Status);
}

inline auto list_entry(IRP *irp)
{
	return &irp->Tail.Overlay.ListEntry;
}
