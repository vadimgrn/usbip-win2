#include "ioctl.h"
#include "trace.h"
#include "ioctl.tmh"

#include <libdrv\dbgcommon.h>

#include "irp.h"
#include "ioctl_vhci.h"
#include "ioctl_vhub.h"

_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
extern "C" PAGEABLE NTSTATUS vhci_ioctl(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp)
{
	PAGED_CODE();

	auto vdev = to_vdev(devobj);

	if (vdev->PnPState == pnp_state::Removed) {
		TraceMsg("%!vdev_type_t! -> NO_SUCH_DEVICE", vdev->type);
		return CompleteRequest(irp, STATUS_NO_SUCH_DEVICE);
	}

	auto irpstack = IoGetCurrentIrpStackLocation(irp);
	auto &ioc = irpstack->Parameters.DeviceIoControl;

	auto inlen = ioc.InputBufferLength;
	auto outlen = ioc.OutputBufferLength;

	TraceDbg("%!hci_version!, %!vdev_type_t! %s(%#08lX), inlen %lu, outlen %lu",
		vdev->version, vdev->type, device_control_name(ioc.IoControlCode), ioc.IoControlCode, inlen, outlen);

	auto buffer = irp->AssociatedIrp.SystemBuffer;
	auto status = STATUS_INVALID_DEVICE_REQUEST;

	switch (vdev->type) {
	case VDEV_VHCI:
		status = vhci_ioctl_vhci(*(vhci_dev_t*)vdev, ioc.IoControlCode, buffer, inlen, outlen);
		break;
	case VDEV_VHUB:
		status = vhci_ioctl_vhub(*(vhub_dev_t*)vdev, ioc.IoControlCode, buffer, inlen, outlen);
		break;
	default:
		Trace(TRACE_LEVEL_WARNING, "ioctl for %!vdev_type_t! is not allowed", vdev->type);
		outlen = 0;
	}

        if (status != STATUS_PENDING) {
                irp->IoStatus.Information = outlen;
                CompleteRequest(irp, status);
	}

	TraceDbg("%!hci_version!, %!vdev_type_t! -> %!STATUS!, outlen %lu", vdev->version, vdev->type, status, outlen);
	return status;
}
