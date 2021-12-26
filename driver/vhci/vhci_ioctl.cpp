#include "vhci.h"
#include "dbgcommon.h"
#include "trace.h"
#include "vhci_ioctl.tmh"

#include "vhci_irp.h"
#include "vhci_ioctl_vhci.h"
#include "vhci_ioctl_vhub.h"

extern "C" PAGEABLE NTSTATUS vhci_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	PAGED_CODE();

	auto buffer = irp->AssociatedIrp.SystemBuffer;

	vdev_t *vdev = devobj_to_vdev(devobj);
	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	ULONG ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	Trace(TRACE_LEVEL_VERBOSE, "%!vdev_type_t!: enter irql %!irql!, %s(%#08lX)",
			vdev->type, KeGetCurrentIrql(), dbg_ioctl_code(ioctl_code), ioctl_code);

	ULONG inlen = irpstack->Parameters.DeviceIoControl.InputBufferLength;
	ULONG outlen = irpstack->Parameters.DeviceIoControl.OutputBufferLength;

	if (vdev->DevicePnPState == Deleted) {
		status = STATUS_NO_SUCH_DEVICE;
		goto END;
	}

	switch (vdev->type) {
	case VDEV_VHCI:
		status = vhci_ioctl_vhci((vhci_dev_t*)vdev, irpstack, ioctl_code, buffer, inlen, &outlen);
		break;
	case VDEV_VHUB:
		status = vhci_ioctl_vhub((vhub_dev_t*)vdev, irp, ioctl_code, buffer, inlen, &outlen);
		break;
	default:
		Trace(TRACE_LEVEL_WARNING, "ioctl for %!vdev_type_t! is not allowed", vdev->type);
		outlen = 0;
	}

	irp->IoStatus.Information = outlen;

END:
	if (status != STATUS_PENDING) {
		irp_done(irp, status);
	}

	Trace(TRACE_LEVEL_VERBOSE, "%!vdev_type_t!: leave %!STATUS!", vdev->type, status);
	return status;
}
