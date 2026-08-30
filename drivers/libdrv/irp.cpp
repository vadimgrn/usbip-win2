/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "irp.h"

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS libdrv::ForwardIrp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
        NT_ASSERT(devobj);
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(devobj, irp);
}

/*
 * A caller must complete the IRP after this call.
 *
 * IoForwardIrpSynchronously only returns FALSE if no next stack location is available in the IRP.
 * This means your driver is at the bottom of the device stack, or the IRP was poorly constructed
 * by the sender. Because the function failed, the IRP was never passed down, and you are
 * responsible for handling its failure lifecycle.
 */
_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS libdrv::ForwardIrpSynchronously(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	PAGED_CODE();
	NT_ASSERT(devobj);

	auto &status = irp->IoStatus.Status;

	if (!IoForwardIrpSynchronously(devobj, irp)) {
                status = STATUS_INVALID_DEVICE_REQUEST;
                irp->IoStatus.Information = 0;
        }

	return status;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS libdrv::CompleteRequest(_In_ IRP *irp, _In_ NTSTATUS status)
{
	irp->IoStatus.Status = status;
	CompleteRequest(irp);
	return status;
}
