#include "vhci.h"
#include "dbgcommon.h"
#include "trace.h"
#include "vhci_ioctl.tmh"

#include "vhci_ioctl_vhci.h"
#include "vhci_ioctl_vhub.h"

PAGEABLE NTSTATUS
vhci_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	pvdev_t	vdev = DEVOBJ_TO_VDEV(devobj);
	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

	PAGED_CODE();

	PIO_STACK_LOCATION irpstack = IoGetCurrentIrpStackLocation(irp);
	ULONG ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	TraceInfo(TRACE_IOCTL, "%!vdev_type_t!: Enter: %s(%#010lX), irp:%p",
		DEVOBJ_VDEV_TYPE(devobj), dbg_ioctl_code(ioctl_code), ioctl_code, irp);

	// Check to see whether the bus is removed
	if (vdev->DevicePnPState == Deleted) {
		status = STATUS_NO_SUCH_DEVICE;
		goto END;
	}

	PVOID buffer = irp->AssociatedIrp.SystemBuffer;

	ULONG inlen = irpstack->Parameters.DeviceIoControl.InputBufferLength;
	ULONG outlen = irpstack->Parameters.DeviceIoControl.OutputBufferLength;

	switch (DEVOBJ_VDEV_TYPE(devobj)) {
	case VDEV_VHCI:
		status = vhci_ioctl_vhci(DEVOBJ_TO_VHCI(devobj), irpstack, ioctl_code, buffer, inlen, &outlen);
		break;
	case VDEV_VHUB:
		status = vhci_ioctl_vhub(DEVOBJ_TO_VHUB(devobj), irp, ioctl_code, buffer, inlen, &outlen);
		break;
	default:
		TraceWarning(TRACE_IOCTL, "ioctl for %!vdev_type_t! is not allowed", DEVOBJ_VDEV_TYPE(devobj));
		outlen = 0;
		break;
	}

	irp->IoStatus.Information = outlen;
END:
	if (status != STATUS_PENDING) {
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
	}

	TraceInfo(TRACE_IOCTL, "Leave: irp:%p, %!STATUS!", irp, status);

	return status;
}
