#include "irp.h"
#include "trace.h"
#include "irp.tmh"

#include "dev.h"

namespace
{

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS on_completion(_In_ PDEVICE_OBJECT, _In_ PIRP irp, _In_ PVOID Context)
{
	if (irp->PendingReturned) {
		KeSetEvent(static_cast<KEVENT*>(Context), IO_NO_INCREMENT, false);
	}

	return StopCompletion;
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS irp_pass_down(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	PAGED_CODE();
	irp->IoStatus.Status = STATUS_SUCCESS; // FIXME: do not touch it?
	IoSkipCurrentIrpStackLocation(irp);
	return IoCallDriver(devobj, irp);
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS irp_pass_down_or_complete(vdev_t *vdev, IRP *irp)
{
	return is_fdo(vdev->type) ? irp_pass_down(vdev->devobj_lower, irp) : CompleteRequest(irp);
}

/*
 * Wait for lower drivers to be done with the Irp.
 * Important thing to note here is when you allocate the memory for an event in the stack 
 * you must do a KernelMode wait instead of UserMode to prevent the stack from getting paged out.
 * 
 * See: IoForwardIrpSynchronously
 */
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS irp_send_synchronously(DEVICE_OBJECT *devobj, IRP *irp)
{
	PAGED_CODE();

	KEVENT evt;
	KeInitializeEvent(&evt, NotificationEvent, false);

	IoCopyCurrentIrpStackLocationToNext(irp);
	IoSetCompletionRoutine(irp, on_completion, &evt, true, true, true);

	auto status = IoCallDriver(devobj, irp);

	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&evt, Executive, KernelMode, false, nullptr);
		status = irp->IoStatus.Status;
	}

	return status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS CompleteRequest(IRP *irp, NTSTATUS status)
{
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS CompleteRequestAsIs(IRP *irp)
{
	auto st = irp->IoStatus.Status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return st;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void complete_as_canceled(IRP *irp)
{
	TraceMsg("%04x", ptr4log(irp));

	irp->IoStatus.Status = STATUS_CANCELLED;
	irp->IoStatus.Information = 0;

	IoCompleteRequest(irp, IO_NO_INCREMENT);
}
