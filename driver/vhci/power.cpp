#include "vhci.h"
#include "trace.h"
#include "power.tmh"

#include "dev.h"
#include "irp.h"

namespace
{

void log_set_power(POWER_STATE_TYPE type, const POWER_STATE *state, const char *caller)
{
	switch (type) {
	case SystemPowerState:
		Trace(TRACE_LEVEL_INFORMATION, "%s: %!SYSTEM_POWER_STATE!", caller, state->SystemState);
		break;
	case DevicePowerState:
		Trace(TRACE_LEVEL_INFORMATION, "%s: %!DEVICE_POWER_STATE!", caller, state->DeviceState);
		break;
	}
}

NTSTATUS
	vhci_power_vhci(vhci_dev_t * vhci, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	POWER_STATE		powerState;
	POWER_STATE_TYPE	powerType;

	powerType = irpstack->Parameters.Power.Type;
	powerState = irpstack->Parameters.Power.State;

	// If the device is not stated yet, just pass it down.
	if (vhci->DevicePnPState == NotStarted) {
		return irp_pass_down(vhci->devobj_lower, irp);
	}

	if (irpstack->MinorFunction == IRP_MN_SET_POWER) {
		log_set_power(powerType, &powerState, __func__);
	}

	return irp_pass_down(vhci->devobj_lower, irp);
}

NTSTATUS
	vhci_power_vdev(vdev_t * vdev, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	POWER_STATE		powerState;
	POWER_STATE_TYPE	powerType;
	NTSTATUS		status;

	powerType = irpstack->Parameters.Power.Type;
	powerState = irpstack->Parameters.Power.State;

	switch (irpstack->MinorFunction) {
	case IRP_MN_SET_POWER:
		log_set_power(powerType, &powerState, __func__);
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
	}

	if (status != STATUS_NOT_SUPPORTED) {
		irp->IoStatus.Status = status;
	}

	return irp_done_iostatus(irp);
}

} // namespace


extern "C" NTSTATUS vhci_power(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	vdev_t *vdev = devobj_to_vdev(devobj);
	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);

	TraceCall("%!vdev_type_t!: irql %!irql!, %!powermn!", 
			vdev->type, KeGetCurrentIrql(), irpstack->MinorFunction);

	if (vdev->DevicePnPState == Deleted) {
		Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!: no such device", vdev->type);
		PoStartNextPowerIrp(irp);
		return irp_done(irp, STATUS_NO_SUCH_DEVICE);
	}

	NTSTATUS st = vdev->type == VDEV_VHCI ?
			vhci_power_vhci((vhci_dev_t*)vdev, irp, irpstack) :
			vhci_power_vdev(vdev, irp, irpstack);

	TraceCall("%!vdev_type_t!: leave %!STATUS!", vdev->type, st);
	return st;
}
