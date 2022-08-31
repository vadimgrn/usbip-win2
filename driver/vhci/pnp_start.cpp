#include "pnp_start.h"
#include <wdm.h>
#include "trace.h"
#include "pnp_start.tmh"

#include "pnp.h"
#include "irp.h"
#include "wmi.h"

#include <initguid.h> // required for GUID definitions
#include "usbip_vhci_api.h"

namespace
{

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS start_vhci(vhci_dev_t *vhci)
{
	PAGED_CODE();

	auto &guid = vhci_guid(vhci->version);

	auto status = IoRegisterDeviceInterface(vhci->pdo, &guid, nullptr, &vhci->DevIntfVhci);
	if (!NT_SUCCESS(status)) {
		Trace(TRACE_LEVEL_ERROR, "Register vhci device interface %!STATUS!", status);
		return status;
	}

	status = IoRegisterDeviceInterface(vhci->pdo, &GUID_DEVINTERFACE_USB_HOST_CONTROLLER, nullptr, &vhci->DevIntfUSBHC);
	if (!NT_SUCCESS(status)) {
		Trace(TRACE_LEVEL_ERROR, "Register USB Host controller device interface %!STATUS!", status);
		return status;
	}

	status = reg_wmi(vhci);
	if (!NT_SUCCESS(status)) {
		Trace(TRACE_LEVEL_ERROR, "reg_wmi %!STATUS!", status);
	}

	return status;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS start_vhub(vhub_dev_t *vhub)
{
	PAGED_CODE();

	auto status = IoRegisterDeviceInterface(vhub->pdo, &GUID_DEVINTERFACE_USB_HUB, nullptr, &vhub->DevIntfRootHub);
	if (NT_ERROR(status)) {
		Trace(TRACE_LEVEL_ERROR, "IoRegisterDeviceInterface %!STATUS!", status);
		return STATUS_UNSUCCESSFUL;
	}

	status = IoSetDeviceInterfaceState(&vhub->DevIntfRootHub, true);
	if (NT_ERROR(status)) {
		Trace(TRACE_LEVEL_ERROR, "IoSetDeviceInterfaceState(RootHub) %!STATUS!", status);
		return STATUS_UNSUCCESSFUL;
	}

	auto vhci = static_cast<vhci_dev_t*>(vhub->parent);

	status = IoSetDeviceInterfaceState(&vhci->DevIntfVhci, true);
	if (!NT_SUCCESS(status)) {
		Trace(TRACE_LEVEL_ERROR, "IoSetDeviceInterfaceState(Vhci) %!STATUS!", status);
		return status;
	}
	
	status = IoSetDeviceInterfaceState(&vhci->DevIntfUSBHC, true);
	if (!NT_SUCCESS(status)) {
		IoSetDeviceInterfaceState(&vhci->DevIntfVhci, false);
		Trace(TRACE_LEVEL_ERROR, "IoSetDeviceInterfaceState(USBHC) %!STATUS!", status);
		return status;
	}
	
	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto start_vpdo(vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	auto iface = &vpdo->usb_dev_interface;
	NT_ASSERT(!iface->Buffer);

	if (auto err = IoRegisterDeviceInterface(vpdo->Self, &GUID_DEVINTERFACE_USB_DEVICE, nullptr, iface)) {
		Trace(TRACE_LEVEL_ERROR, "IoRegisterDeviceInterface %!STATUS!", err);
		return err;
	}

	if (auto err = IoSetDeviceInterfaceState(iface, true)) {
		Trace(TRACE_LEVEL_ERROR, "IoSetDeviceInterfaceState('%!USTR!') %!STATUS!", iface, err);
		return err;
	}

	TraceMsg("SymbolicLinkName '%!USTR!'", iface);
	return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS pnp_start_device(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();

	if (is_fdo(vdev->type)) {
		auto status = irp_send_synchronously(vdev->devobj_lower, irp);
		if (NT_ERROR(status)) {
			return CompleteRequest(irp, status);
		}
	}

	auto status = STATUS_SUCCESS;

	switch (vdev->type) {
	case VDEV_VHCI:
		status = start_vhci((vhci_dev_t*)vdev);
		break;
	case VDEV_VHUB:
		status = start_vhub((vhub_dev_t*)vdev);
		break;
	case VDEV_VPDO:
		status = start_vpdo((vpdo_dev_t*)vdev);
		break;
	}

	if (NT_SUCCESS(status)) {
		set_state(*vdev, pnp_state::Started);

		POWER_STATE ps{ .DeviceState = PowerDeviceD0 };
		vdev->DevicePowerState = ps.DeviceState;
		PoSetPowerState(vdev->Self, DevicePowerState, ps);

		Trace(TRACE_LEVEL_INFORMATION, "%!hci_version!, %!vdev_type_t! started", vdev->version, vdev->type);
	}

	return CompleteRequest(irp, status);
}
