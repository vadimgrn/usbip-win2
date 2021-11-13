#include "vhci.h"
#include "trace.h"
#include "vhci_power.tmh"

#include "vhci_dev.h"
#include "vhci_irp.h"

static NTSTATUS
vhci_power_vhci(pvhci_dev_t vhci, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	POWER_STATE		powerState;
	POWER_STATE_TYPE	powerType;

	powerType = irpstack->Parameters.Power.Type;
	powerState = irpstack->Parameters.Power.State;

	// If the device is not stated yet, just pass it down.
	if (vhci->common.DevicePnPState == NotStarted) {
		return irp_pass_down(vhci->common.devobj_lower, irp);
	}

	if (irpstack->MinorFunction == IRP_MN_SET_POWER) {
		TraceInfo(DBG_POWER, "\tRequest to set %s state to %s\n",
			(powerType == SystemPowerState ? "System" : "Device"),
			(powerType == SystemPowerState ? 
				dbg_system_power(powerState.SystemState) : 
				dbg_device_power(powerState.DeviceState)));
	}

	return irp_pass_down(vhci->common.devobj_lower, irp);
}

static NTSTATUS
vhci_power_vdev(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	POWER_STATE		powerState;
	POWER_STATE_TYPE	powerType;
	NTSTATUS		status;

	powerType = irpstack->Parameters.Power.Type;
	powerState = irpstack->Parameters.Power.State;

	switch (irpstack->MinorFunction) {
	case IRP_MN_SET_POWER:
		TraceInfo(DBG_POWER, "\tSetting %s power state to %s\n",
			(powerType == SystemPowerState ? "System" : "Device"),
			(powerType == SystemPowerState ?
				dbg_system_power(powerState.SystemState) :
				dbg_device_power(powerState.DeviceState)));

		switch (powerType) {
		case DevicePowerState:
			PoSetPowerState(vdev->Self, powerType, powerState);
			vdev->DevicePowerState = powerState.DeviceState;
			status = STATUS_SUCCESS;
			break;

		case SystemPowerState:
			vdev->SystemPowerState = powerState.SystemState;
			status = STATUS_SUCCESS;
			break;

		default:
			status = STATUS_NOT_SUPPORTED;
			break;
		}
		break;
	case IRP_MN_QUERY_POWER:
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_WAIT_WAKE:
		// We cannot support wait-wake because we are root-enumerated
		// driver, and our parent, the PnP manager, doesn't support wait-wake.
		// If you are a bus enumerated device, and if  your parent bus supports
		// wait-wake,  you should send a wait/wake IRP (PoRequestPowerIrp)
		// in response to this request.
		// If you want to test the wait/wake logic implemented in the function
		// driver (USBIP.sys), you could do the following simulation:
		// a) Mark this IRP pending.
		// b) Set a cancel routine.
		// c) Save this IRP in the device extension
		// d) Return STATUS_PENDING.
		// Later on if you suspend and resume your system, your vhci_power()
		// will be called to power the bus. In response to IRP_MN_SET_POWER, if the
		// powerstate is PowerSystemWorking, complete this Wake IRP.
		// If the function driver, decides to cancel the wake IRP, your cancel routine
		// will be called. There you just complete the IRP with STATUS_CANCELLED.
	case IRP_MN_POWER_SEQUENCE:
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	if (status != STATUS_NOT_SUPPORTED) {
		irp->IoStatus.Status = status;
	}

	status = irp->IoStatus.Status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}

NTSTATUS
vhci_power(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	pvdev_t	vdev = DEVOBJ_TO_VDEV(devobj);
	PIO_STACK_LOCATION	irpstack;
	NTSTATUS		status;

	irpstack = IoGetCurrentIrpStackLocation(irp);

	TraceInfo(DBG_POWER, "%s: Enter: minor:%s\n",
		dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)), dbg_power_minor(irpstack->MinorFunction));

	status = STATUS_SUCCESS;

	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	if (vdev->DevicePnPState == Deleted) {
		PoStartNextPowerIrp(irp);
		irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return status;
	}

	switch (DEVOBJ_VDEV_TYPE(devobj)) {
	case VDEV_VHCI:
		status = vhci_power_vhci((pvhci_dev_t)vdev, irp, irpstack);
		break;
	default:
		status = vhci_power_vdev(vdev, irp, irpstack);
		break;
	}

	TraceInfo(DBG_POWER, "%s: Leave: status: %s\n",
		dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)), dbg_ntstatus(status));

	return status;
}
