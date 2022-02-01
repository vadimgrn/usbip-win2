#include "irp.h"

namespace
{

NTSTATUS irp_completion_routine(__in PDEVICE_OBJECT, __in PIRP irp, __in PVOID Context)
{
	// If the lower driver didn't return STATUS_PENDING, we don't need to
	// set the event because we won't be waiting on it.
	// This optimization avoids grabbing the dispatcher lock and improves perf.
	if (irp->PendingReturned) {
		KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
	}
	return STATUS_MORE_PROCESSING_REQUIRED; // Keep this IRP
}

} // namespace


PAGEABLE NTSTATUS irp_pass_down(DEVICE_OBJECT *devobj, IRP *irp)
{
	PAGED_CODE();
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoSkipCurrentIrpStackLocation(irp);
	return IoCallDriver(devobj, irp);
}

/*
 * Wait for lower drivers to be done with the Irp.
 * Important thing to note here is when you allocate the memory for an event in the stack 
 * you must do a KernelMode wait instead of UserMode to prevent the stack from getting paged out.
 */
PAGEABLE NTSTATUS irp_send_synchronously(DEVICE_OBJECT *devobj, IRP *irp)
{
	PAGED_CODE();

	KEVENT event;
	KeInitializeEvent(&event, NotificationEvent, FALSE);

	IoCopyCurrentIrpStackLocationToNext(irp);
	IoSetCompletionRoutine(irp, irp_completion_routine, &event, TRUE, TRUE, TRUE);

	auto status = IoCallDriver(devobj, irp);

	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, nullptr);
		status = irp->IoStatus.Status;
	}

	return status;
}

NTSTATUS irp_done(IRP *irp, NTSTATUS status)
{
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}
