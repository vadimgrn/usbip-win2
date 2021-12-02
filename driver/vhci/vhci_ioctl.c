#include "vhci.h"
#include "dbgcommon.h"
#include "trace.h"
#include "vhci_ioctl.tmh"

#include "vhci_irp.h"
#include "vhci_ioctl_vhci.h"
#include "vhci_ioctl_vhub.h"

PAGEABLE NTSTATUS
vhci_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	PAGED_CODE();

	vdev_t *vdev = devobj_to_vdev(devobj);
	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	ULONG ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	TraceInfo(TRACE_IOCTL, "%!vdev_type_t!: enter irp %p, irql %!irql!, %s(%#010lX)", 
		vdev->type, irp, KeGetCurrentIrql(), dbg_ioctl_code(ioctl_code), ioctl_code);

	if (vdev->DevicePnPState == Deleted) {
		status = STATUS_NO_SUCH_DEVICE;
		goto END;
	}

	PVOID buffer = irp->AssociatedIrp.SystemBuffer;

	ULONG inlen = irpstack->Parameters.DeviceIoControl.InputBufferLength;
	ULONG outlen = irpstack->Parameters.DeviceIoControl.OutputBufferLength;

	switch (vdev->type) {
	case VDEV_VHCI:
		status = vhci_ioctl_vhci((vhci_dev_t*)vdev, irpstack, ioctl_code, buffer, inlen, &outlen);
		break;
	case VDEV_VHUB:
		status = vhci_ioctl_vhub((vhub_dev_t*)vdev, irp, ioctl_code, buffer, inlen, &outlen);
		break;
	default:
		TraceWarning(TRACE_IOCTL, "ioctl for %!vdev_type_t! is not allowed", vdev->type);
		outlen = 0;
	}

	irp->IoStatus.Information = outlen;

END:
	if (status != STATUS_PENDING) {
		irp_done(irp, status);
	}

	TraceInfo(TRACE_IOCTL, "Leave irp %p, %!STATUS!", irp, status);
	return status;
}
