#include "vhci.h"
#include "dbgcommon.h"
#include "trace.h"
#include "ioctl.tmh"

#include "irp.h"
#include "ioctl_vhci.h"
#include "ioctl_vhub.h"

extern "C" PAGEABLE NTSTATUS vhci_ioctl(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
	PAGED_CODE();

	auto vdev = devobj_to_vdev(devobj);
	auto status = STATUS_INVALID_DEVICE_REQUEST;

	auto irpstack = IoGetCurrentIrpStackLocation(irp);
	auto ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	TraceCall("%!vdev_type_t!: enter irql %!irql!, %s(%#08lX)",
			vdev->type, KeGetCurrentIrql(), dbg_ioctl_code(ioctl_code), ioctl_code);

	auto buffer = irp->AssociatedIrp.SystemBuffer;

	auto inlen = irpstack->Parameters.DeviceIoControl.InputBufferLength;
	auto outlen = irpstack->Parameters.DeviceIoControl.OutputBufferLength;

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

	TraceCall("%!vdev_type_t!: leave %!STATUS!", vdev->type, status);
	return status;
}