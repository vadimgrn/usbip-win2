#include "vhci_pnp_cap.h"
#include "trace.h"
#include "vhci_pnp_cap.tmh"

#include "vhci_irp.h"

static PAGEABLE NTSTATUS
get_device_capabilities(PDEVICE_OBJECT devobj, PDEVICE_CAPABILITIES pcaps)
{
	IO_STATUS_BLOCK		ioStatus;
	PIO_STACK_LOCATION	irpstack;
	KEVENT		pnpEvent;
	PIRP		irp;
	NTSTATUS	status;

	PAGED_CODE();

	// Initialize the capabilities that we will send down
	RtlZeroMemory(pcaps, sizeof(DEVICE_CAPABILITIES));
	pcaps->Size = sizeof(DEVICE_CAPABILITIES);
	pcaps->Version = 1;
	pcaps->Address = (ULONG)-1;
	pcaps->UINumber = (ULONG)-1;

	KeInitializeEvent(&pnpEvent, NotificationEvent, FALSE);

	// Build an Irp
	irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, devobj, NULL, 0, NULL, &pnpEvent, &ioStatus);
	if (irp == NULL) {
		TraceWarning(TRACE_PNP, "failed to create irp");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// Pnp Irps all begin life as STATUS_NOT_SUPPORTED;
	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	irpstack = IoGetNextIrpStackLocation(irp);

	// Set the top of stack
	RtlZeroMemory(irpstack, sizeof(IO_STACK_LOCATION));
	irpstack->MajorFunction = IRP_MJ_PNP;
	irpstack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
	irpstack->Parameters.DeviceCapabilities.Capabilities = pcaps;

	status = IoCallDriver(devobj, irp);
	if (status == STATUS_PENDING) {
		// Block until the irp comes back.
		// Important thing to note here is when you allocate
		// the memory for an event in the stack you must do a
		// KernelMode wait instead of UserMode to prevent
		// the stack from getting paged out.
		KeWaitForSingleObject(&pnpEvent, Executive, KernelMode, FALSE, NULL);
		status = ioStatus.Status;
	}

	return status;
}

static PAGEABLE void
setup_capabilities(PDEVICE_CAPABILITIES pcaps)
{
	pcaps->LockSupported = FALSE;
	pcaps->EjectSupported = FALSE;
	pcaps->Removable = FALSE;
	pcaps->DockDevice = FALSE;
	pcaps->UniqueID = FALSE;
	pcaps->SilentInstall = FALSE;
	pcaps->SurpriseRemovalOK = FALSE;

	pcaps->Address = 1;
	pcaps->UINumber = 1;
}

static PAGEABLE NTSTATUS
pnp_query_cap_vpdo(pvpdo_dev_t vpdo, PIO_STACK_LOCATION irpstack)
{
	PDEVICE_CAPABILITIES	pcaps;
	DEVICE_CAPABILITIES	caps_parent;
	NTSTATUS		status;

	PAGED_CODE();

	pcaps = irpstack->Parameters.DeviceCapabilities.Capabilities;

	// Set the capabilities.
	if (pcaps->Version != 1 || pcaps->Size < sizeof(DEVICE_CAPABILITIES)) {
		TraceWarning(TRACE_PNP, "invalid device capabilities: version: %u, size: %u", pcaps->Version, pcaps->Size);
		return STATUS_UNSUCCESSFUL;
	}

	// Get the device capabilities of the root pdo
	status = get_device_capabilities(vpdo->common.parent->parent->parent->devobj_lower, &caps_parent);
	if (!NT_SUCCESS(status)) {
		TraceError(TRACE_PNP, "failed to get device capabilities from root device: %!STATUS!", status);
		return status;
	}

	// The entries in the DeviceState array are based on the capabilities
	// of the parent devnode. These entries signify the highest-powered
	// state that the device can support for the corresponding system
	// state. A driver can specify a lower (less-powered) state than the
	// bus driver.  For eg: Suppose the USBIP bus controller supports
	// D0, D2, and D3; and the USBIP Device supports D0, D1, D2, and D3.
	// Following the above rule, the device cannot specify D1 as one of
	// it's power state. A driver can make the rules more restrictive
	// but cannot loosen them.
	// First copy the parent's S to D state mapping
	RtlCopyMemory(pcaps->DeviceState, caps_parent.DeviceState, (PowerSystemShutdown + 1) * sizeof(DEVICE_POWER_STATE));

	// Adjust the caps to what your device supports.
	// Our device just supports D0 and D3.
	pcaps->DeviceState[PowerSystemWorking] = PowerDeviceD0;

	if (pcaps->DeviceState[PowerSystemSleeping1] != PowerDeviceD0)
		pcaps->DeviceState[PowerSystemSleeping1] = PowerDeviceD1;

	if (pcaps->DeviceState[PowerSystemSleeping2] != PowerDeviceD0)
		pcaps->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;

	if (pcaps->DeviceState[PowerSystemSleeping3] != PowerDeviceD0)
		pcaps->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;

	// We can wake the system from D1
	pcaps->DeviceWake = PowerDeviceD0;

	// Specifies whether the device hardware supports the D1 and D2
	// power state. Set these bits explicitly.
	pcaps->DeviceD1 = FALSE; // Yes we can
	pcaps->DeviceD2 = FALSE;

	// Specifies whether the device can respond to an external wake
	// signal while in the D0, D1, D2, and D3 state.
	// Set these bits explicitly.
	pcaps->WakeFromD0 = TRUE;
	pcaps->WakeFromD1 = FALSE; //Yes we can
	pcaps->WakeFromD2 = FALSE;
	pcaps->WakeFromD3 = FALSE;

	// We have no latencies
	pcaps->D1Latency = 0;
	pcaps->D2Latency = 0;
	pcaps->D3Latency = 0;

	// Ejection supported
	pcaps->EjectSupported = FALSE;

	// This flag specifies whether the device's hardware is disabled.
	// The PnP Manager only checks this bit right after the device is
	// enumerated. Once the device is started, this bit is ignored.
	pcaps->HardwareDisabled = FALSE;

	// Our simulated device can be physically removed.
	pcaps->Removable = TRUE;

	// Setting it to TRUE prevents the warning dialog from appearing
	// whenever the device is surprise removed.
	pcaps->SurpriseRemovalOK = TRUE;

	// If a custom instance id is used, assume that it is system-wide unique */
	pcaps->UniqueID = vpdo->serial_usr || vpdo->serial;

	// Specify whether the Device Manager should suppress all
	// installation pop-ups except required pop-ups such as
	// "no compatible drivers found."
	pcaps->SilentInstall = FALSE;

	// Specifies an address indicating where the device is located
	// on its underlying bus. The interpretation of this number is
	// bus-specific. If the address is unknown or the bus driver
	// does not support an address, the bus driver leaves this
	// member at its default value of 0xFFFFFFFF. In this example
	// the location address is same as instance id.
	pcaps->Address = vpdo->port;

	// UINumber specifies a number associated with the device that can
	// be displayed in the user interface.
	pcaps->UINumber = vpdo->port;

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
pnp_query_cap(PIO_STACK_LOCATION irpstack)
{
	PDEVICE_CAPABILITIES	pcaps;

	PAGED_CODE();

	pcaps = irpstack->Parameters.DeviceCapabilities.Capabilities;

	// Set the capabilities.
	if (pcaps->Version != 1 || pcaps->Size < sizeof(DEVICE_CAPABILITIES)) {
		TraceWarning(TRACE_PNP, "invalid device capabilities: version: %u, size: %u", pcaps->Version, pcaps->Size);
		return STATUS_UNSUCCESSFUL;
	}
	setup_capabilities(pcaps);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
pnp_query_capabilities(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	NTSTATUS	status = irp->IoStatus.Status;

	if (is_fdo(vdev->type)) {
		return irp_pass_down(vdev->devobj_lower, irp);
	}

	if (vdev->type == VDEV_VPDO) {
		status = pnp_query_cap_vpdo((pvpdo_dev_t)vdev, irpstack);
	} else {
		status = pnp_query_cap(irpstack);
	}

	return irp_done(irp, status);
}