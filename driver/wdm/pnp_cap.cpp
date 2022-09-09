#include "pnp_cap.h"
#include "dev.h"
#include "trace.h"
#include "pnp_cap.tmh"

#include "irp.h"

namespace
{

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto get_device_capabilities(_Out_ DEVICE_CAPABILITIES &r, _In_ DEVICE_OBJECT *devobj)
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
        auto irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, devobj, nullptr, 0, nullptr, &pnpEvent, &ios); // must not call IoFreeIrp
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
PAGEABLE auto set_state(_Inout_ DEVICE_POWER_STATE st[POWER_SYSTEM_MAXIMUM])
{
	PAGED_CODE();

	st[PowerSystemWorking] = PowerDeviceD0;

	if (st[PowerSystemSleeping1] != PowerDeviceD0) {
		st[PowerSystemSleeping1] = PowerDeviceD1;
	}

	if (st[PowerSystemSleeping2] != PowerDeviceD0) {
		st[PowerSystemSleeping2] = PowerDeviceD3;
	}

	if (st[PowerSystemSleeping3] != PowerDeviceD0) {
		st[PowerSystemSleeping3] = PowerDeviceD3;
	}
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto pnp_query_cap(_Inout_ vpdo_dev_t &vpdo, _Inout_ DEVICE_CAPABILITIES &r)
{
	PAGED_CODE();

	DEVICE_CAPABILITIES caps_parent{};

	if (auto err = get_device_capabilities(caps_parent, vpdo.parent->parent->parent->devobj_lower)) {
		Trace(TRACE_LEVEL_ERROR, "Failed to get device capabilities from root device: %!STATUS!", err);
		return err;
	} else {
		RtlCopyMemory(r.DeviceState, caps_parent.DeviceState, sizeof(r.DeviceState));
		set_state(r.DeviceState);
	}

	r.LockSupported = false;
	r.EjectSupported = false; // see IoRequestDeviceEject
	r.Removable = true; // must be set

	// If a custom instance id is used, assume that it is system-wide unique */
	r.UniqueID = vpdo.serial.Length || get_serial_number(vpdo);

//	r.RawDeviceOK = false;
	r.SilentInstall = false;
	r.SurpriseRemovalOK = false;
	r.HardwareDisabled = false;

	r.Address = vpdo.port;
	r.UINumber = vpdo.port;

	r.DeviceWake = PowerDeviceD0;

	r.D1Latency = 0; // does not support D1 state
	r.D2Latency = 0; // does not support D2 state
	r.D3Latency = 1; // 100-microsecond units

	r.DeviceD1 = false;
	r.DeviceD2 = false;

	r.WakeFromD0 = true;
	r.WakeFromD1 = false;
	r.WakeFromD2 = false;
	r.WakeFromD3 = false;

	TraceMsg("UniqueID %d, Address %#lx, UINumber %lu", r.UniqueID, r.Address, r.UINumber);
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

	TraceMsg("Address %#lx, UINumber %lu", r.Address, r.UINumber);
	return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS pnp_query_capabilities(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();
	TraceDbg("vdev %04x, %!vdev_type_t!", ptr4log(vdev), vdev->type);

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
