#include "pnp_cap.h"
#include "dev.h"
#include "trace.h"
#include "pnp_cap.tmh"

#include "irp.h"

namespace
{

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto get_device_capabilities(_In_ DEVICE_OBJECT *devobj, _Out_ DEVICE_CAPABILITIES &r)
{
	PAGED_CODE();

	RtlZeroMemory(&r, sizeof(r));

	r.Size = sizeof(r);
	r.Version = 1;

	r.Address = ULONG(-1);
	r.UINumber = ULONG(-1);

        KEVENT pnpEvent;
        KeInitializeEvent(&pnpEvent, NotificationEvent, false);

        IO_STATUS_BLOCK ios{};
        auto irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, devobj, nullptr, 0, nullptr, &pnpEvent, &ios);
	if (!irp) {
		Trace(TRACE_LEVEL_ERROR, "IoBuildSynchronousFsdRequest error");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	auto irpstack = IoGetNextIrpStackLocation(irp);

	// Set the top of stack
	RtlZeroMemory(irpstack, sizeof(*irpstack));
	irpstack->MajorFunction = IRP_MJ_PNP;
	irpstack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
	irpstack->Parameters.DeviceCapabilities.Capabilities = &r;

	auto status = IoCallDriver(devobj, irp);
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&pnpEvent, Executive, KernelMode, false, nullptr);
		status = ios.Status;
	}

	return status;
}

/*
 * The entries in the DeviceState array are based on the capabilities
 * of the parent devnode. These entries signify the highest-powered
 * state that the device can support for the corresponding system
 * state. A driver can specify a lower (less-powered) state than the
 * bus driver.  For eg: Suppose the USBIP bus controller supports
 * D0, D2, and D3; and the USBIP Device supports D0, D1, D2, and D3.
 * Following the above rule, the device cannot specify D1 as one of
 * it's power state. A driver can make the rules more restrictive
 * but cannot loosen them.
 * Our device just supports D0 and D3.
 */
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto pnp_query_cap(_Inout_ vpdo_dev_t &vpdo, _Inout_ DEVICE_CAPABILITIES &r)
{
	PAGED_CODE();

	DEVICE_CAPABILITIES caps_parent{};

	auto status = get_device_capabilities(vpdo.parent->parent->parent->devobj_lower, caps_parent);
	if (!NT_SUCCESS(status)) {
		Trace(TRACE_LEVEL_ERROR, "Failed to get device capabilities from root device: %!STATUS!", status);
		return status;
	}

	RtlCopyMemory(r.DeviceState, caps_parent.DeviceState, sizeof(r.DeviceState));

	r.DeviceState[PowerSystemWorking] = PowerDeviceD0;

	if (r.DeviceState[PowerSystemSleeping1] != PowerDeviceD0) {
		r.DeviceState[PowerSystemSleeping1] = PowerDeviceD1;
	}

	if (r.DeviceState[PowerSystemSleeping2] != PowerDeviceD0) {
		r.DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
	}

	if (r.DeviceState[PowerSystemSleeping3] != PowerDeviceD0) {
		r.DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
	}

	// We can wake the system from D1
	r.DeviceWake = PowerDeviceD0;

	// Specifies whether the device hardware supports the D1 and D2
	// power state. Set these bits explicitly.
	r.DeviceD1 = false; // Yes we can
	r.DeviceD2 = false;

	// Specifies whether the device can respond to an external wake
	// signal while in the D0, D1, D2, and D3 state.
	// Set these bits explicitly.
	r.WakeFromD0 = true;
	r.WakeFromD1 = false; // Yes we can
	r.WakeFromD2 = false;
	r.WakeFromD3 = false;

	// We have no latencies
	r.D1Latency = 0;
	r.D2Latency = 0;
	r.D3Latency = 0;

	r.EjectSupported = false;

	// This flag specifies whether the device's hardware is disabled.
	// The PnP Manager only checks this bit right after the device is
	// enumerated. Once the device is started, this bit is ignored.
	r.HardwareDisabled = false;

	// Our simulated device can be physically removed.
	r.Removable = true;

	// Setting it to true prevents the warning dialog from appearing
	// whenever the device is surprise removed.
	r.SurpriseRemovalOK = true;

	// If a custom instance id is used, assume that it is system-wide unique */
	r.UniqueID = vpdo.serial.Length || get_serial_number(vpdo);

	// Specify whether the Device Manager should suppress all
	// installation pop-ups except required pop-ups such as
	// "no compatible drivers found."
	r.SilentInstall = false;

	// Specifies an address indicating where the device is located
	// on its underlying bus. The interpretation of this number is
	// bus-specific. If the address is unknown or the bus driver
	// does not support an address, the bus driver leaves this
	// member at its default value of 0xFFFFFFFF. In this example
	// the location address is same as instance id.
	r.Address = vpdo.port;

	// UINumber specifies a number associated with the device that can
	// be displayed in the user interface.
	r.UINumber = vpdo.port;

	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto pnp_query_cap(_Inout_ DEVICE_CAPABILITIES &r)
{
	PAGED_CODE();

	r.LockSupported = false;
	r.EjectSupported = false;
	r.Removable = false;
	r.DockDevice = false;
	r.UniqueID = false;
	r.SilentInstall = false;
	r.SurpriseRemovalOK = false;

	r.Address = 1;
	r.UINumber = 1;

	return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS pnp_query_capabilities(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();

	if (is_fdo(vdev->type)) {
		return irp_pass_down(vdev->devobj_lower, irp);
	}

	auto st = STATUS_INVALID_PARAMETER;

	auto irpstack = IoGetCurrentIrpStackLocation(irp);
	auto &r = *irpstack->Parameters.DeviceCapabilities.Capabilities;

	if (r.Version == 1 && r.Size == sizeof(r)) {
		st = vdev->type == VDEV_VPDO ? pnp_query_cap(*static_cast<vpdo_dev_t*>(vdev), r) : pnp_query_cap(r);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Version %d, Size %d", r.Version, r.Size);
	}

	return CompleteRequest(irp, st);
}
