/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "irp.h"
#include "trace.h"
#include "irp.tmh"

namespace
{

_Function_class_(IO_COMPLETION_ROUTINE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS on_completion(
	_In_ DEVICE_OBJECT*, _In_ IRP *irp, _In_reads_opt_(_Inexpressible_("varies")) void *context)
{
	if (irp->PendingReturned) {
		KeSetEvent(static_cast<KEVENT*>(context), IO_NO_INCREMENT, false);
	}

	return StopCompletion;
}

} // namespace


/*
 * A caller must complete the IRP after this call because IRP completion is stopped 
 * by returning StopCompletion from CompletionRoutine.
 * @see IoForwardIrpSynchronously
 */
_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS usbip::ForwardIrpAndWait(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	PAGED_CODE();
	NT_ASSERT(devobj);

	KEVENT evt;
	KeInitializeEvent(&evt, NotificationEvent, false);

	IoCopyCurrentIrpStackLocationToNext(irp);
	
	if (auto err = IoSetCompletionRoutineEx(devobj, irp, on_completion, &evt, true, true, true)) {
		Trace(TRACE_LEVEL_ERROR, "IoSetCompletionRoutineEx %!STATUS!", err);
		return err;
	}

	auto status = IoCallDriver(devobj, irp);

	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&evt, Executive, KernelMode, false, nullptr);
		status = irp->IoStatus.Status;
	}

	return status;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::ForwardIrp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	NT_ASSERT(devobj);
	IoSkipCurrentIrpStackLocation(irp);
	return IoCallDriver(devobj, irp);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::CompleteRequest(_In_ IRP *irp, _In_ NTSTATUS status)
{
	irp->IoStatus.Status = status;
	CompleteRequest(irp);
	return status;
}
