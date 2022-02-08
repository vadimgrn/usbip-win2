#include "dbgcommon.h"
#include "trace.h"
#include "ioctl.tmh"

#include "irp.h"
#include "ioctl_vhci.h"
#include "ioctl_vhub.h"

extern "C" PAGEABLE NTSTATUS vhci_ioctl(__in DEVICE_OBJECT *devobj, __in IRP *irp)
{
	PAGED_CODE();

	auto vdev = to_vdev(devobj);

	if (vdev->PnPState == pnp_state::Removed) {
		TraceCall("%!vdev_type_t! -> NO_SUCH_DEVICE", vdev->type);
		return CompleteRequest(irp, STATUS_NO_SUCH_DEVICE);
	}

	auto irpstack = IoGetCurrentIrpStackLocation(irp);
	auto &ioc = irpstack->Parameters.DeviceIoControl;

	TraceCall("%!vdev_type_t!: enter irql %!irql!, %s(%#08lX)",
			vdev->type, KeGetCurrentIrql(), dbg_ioctl_code(ioc.IoControlCode), ioc.IoControlCode);

	auto inlen = ioc.InputBufferLength;
	auto outlen = ioc.OutputBufferLength;

	auto buffer = irp->AssociatedIrp.SystemBuffer;
	auto status = STATUS_INVALID_DEVICE_REQUEST;

	switch (vdev->type) {
	case VDEV_VHCI:
		status = vhci_ioctl_vhci((vhci_dev_t*)vdev, irpstack, ioc.IoControlCode, buffer, inlen, &outlen);
		break;
	case VDEV_VHUB:
		status = vhci_ioctl_vhub((vhub_dev_t*)vdev, irp, ioc.IoControlCode, buffer, inlen, &outlen);
		break;
	default:
		Trace(TRACE_LEVEL_WARNING, "ioctl for %!vdev_type_t! is not allowed", vdev->type);
		outlen = 0;
	}

	irp->IoStatus.Information = outlen;

	if (status != STATUS_PENDING) {
		CompleteRequest(irp, status);
	}

	TraceCall("%!vdev_type_t!: leave %!STATUS!", vdev->type, status);
	return status;
}
