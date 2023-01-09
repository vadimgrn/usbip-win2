/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
 */
_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS libdrv::ForwardIrpSynchronously(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	PAGED_CODE();

	auto &status = irp->IoStatus.Status;

	if (!IoForwardIrpSynchronously(devobj, irp)) {
		status = STATUS_NO_MORE_ENTRIES;
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
