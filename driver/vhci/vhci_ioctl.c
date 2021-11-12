#include "vhci.h"
#include "trace.h"
#include "vhci_ioctl.tmh"

#include "vhci_ioctl_vhci.h"
#include "vhci_ioctl_vhub.h"

PAGEABLE NTSTATUS
vhci_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	pvdev_t	vdev = DEVOBJ_TO_VDEV(devobj);
	PIO_STACK_LOCATION	irpstack;
	ULONG		ioctl_code;
	PVOID		buffer;
	ULONG		inlen, outlen;
	NTSTATUS	status = STATUS_INVALID_DEVICE_REQUEST;

	PAGED_CODE();

	irpstack = IoGetCurrentIrpStackLocation(irp);
	ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	DBGI(DBG_IOCTL, "%s: Enter: code:%s, irp:%p\n",
		dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)), dbg_vhci_ioctl_code(ioctl_code), irp);

	// Check to see whether the bus is removed
	if (vdev->DevicePnPState == Deleted) {
		status = STATUS_NO_SUCH_DEVICE;
		goto END;
	}

	buffer = irp->AssociatedIrp.SystemBuffer;
	inlen = irpstack->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpstack->Parameters.DeviceIoControl.OutputBufferLength;

	switch (DEVOBJ_VDEV_TYPE(devobj)) {
	case VDEV_VHCI:
		status = vhci_ioctl_vhci(DEVOBJ_TO_VHCI(devobj), irpstack, ioctl_code, buffer, inlen, &outlen);
		break;
	case VDEV_VHUB:
		status = vhci_ioctl_vhub(DEVOBJ_TO_VHUB(devobj), irp, ioctl_code, buffer, inlen, &outlen);
		break;
	default:
		DBGW(DBG_IOCTL, "ioctl for %s is not allowed\n", dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)));
		outlen = 0;
		break;
	}

	irp->IoStatus.Information = outlen;
END:
	if (status != STATUS_PENDING) {
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
	}

	DBGI(DBG_IOCTL, "Leave: irp:%p, status:%s\n", irp, dbg_ntstatus(status));

	return status;
}
