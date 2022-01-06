#include "pnp_start.h"
#include "trace.h"
#include "pnp_start.tmh"

#include "pnp.h"
#include "irp.h"
#include "wmi.h"

#include <initguid.h> // required for GUID definitions
#include "usbip_vhci_api.h"

namespace
{

PAGEABLE NTSTATUS start_vhci(vhci_dev_t * vhci)
{
	PAGED_CODE();

	NTSTATUS status = IoRegisterDeviceInterface(vhci->pdo, (LPGUID)&GUID_DEVINTERFACE_VHCI_USBIP, nullptr, &vhci->DevIntfVhci);
	if (!NT_SUCCESS(status)) {
		Trace(TRACE_LEVEL_ERROR, "failed to register vhci device interface: %!STATUS!", status);
		return status;
	}
	status = IoRegisterDeviceInterface(vhci->pdo, (LPGUID)&GUID_DEVINTERFACE_USB_HOST_CONTROLLER, nullptr, &vhci->DevIntfUSBHC);
	if (!NT_SUCCESS(status)) {
		Trace(TRACE_LEVEL_ERROR, "failed to register USB Host controller device interface: %!STATUS!", status);
		return status;
	}

	// Register with WMI
	status = reg_wmi(vhci);
	if (!NT_SUCCESS(status)) {
		Trace(TRACE_LEVEL_ERROR, "reg_wmi failed: %!STATUS!", status);
	}

	return status;
}

PAGEABLE NTSTATUS start_vhub(vhub_dev_t * vhub)
{
	PAGED_CODE();

	vhci_dev_t *	vhci;
	NTSTATUS	status;

	status = IoRegisterDeviceInterface(vhub->pdo, (LPGUID)&GUID_DEVINTERFACE_USB_HUB, nullptr, &vhub->DevIntfRootHub);
	if (NT_ERROR(status)) {
		Trace(TRACE_LEVEL_ERROR, "failed to register USB root hub device interface: %!STATUS!", status);
		return STATUS_UNSUCCESSFUL;
	}
	status = IoSetDeviceInterfaceState(&vhub->DevIntfRootHub, TRUE);
	if (NT_ERROR(status)) {
		Trace(TRACE_LEVEL_ERROR, "failed to activate USB root hub device interface: %!STATUS!", status);
		return STATUS_UNSUCCESSFUL;
	}

	vhci = (vhci_dev_t *)vhub->parent;
	status = IoSetDeviceInterfaceState(&vhci->DevIntfVhci, TRUE);
	if (!NT_SUCCESS(status)) {
		Trace(TRACE_LEVEL_ERROR, "failed to enable vhci device interface: %!STATUS!", status);
		return status;
	}
	status = IoSetDeviceInterfaceState(&vhci->DevIntfUSBHC, TRUE);
	if (!NT_SUCCESS(status)) {
		IoSetDeviceInterfaceState(&vhci->DevIntfVhci, FALSE);
		Trace(TRACE_LEVEL_ERROR, "failed to enable USB host controller device interface: %!STATUS!", status);
		return status;
	}
	return STATUS_SUCCESS;
}

PAGEABLE auto start_vpdo(vpdo_dev_t *vpdo)
{
	PAGED_CODE();

	auto iface = &vpdo->usb_dev_interface;
	NT_ASSERT(!iface->Buffer);

	if (auto err = IoRegisterDeviceInterface(to_devobj(vpdo), &GUID_DEVINTERFACE_USB_DEVICE, nullptr, iface)) {
		Trace(TRACE_LEVEL_ERROR, "IoRegisterDeviceInterface %!STATUS!", err);
		return err;
	}

	if (auto err = IoSetDeviceInterfaceState(iface, TRUE)) {
		Trace(TRACE_LEVEL_ERROR, "IoSetDeviceInterfaceState('%!USTR!') %!STATUS!", iface, err);
		return err;
	}

	TraceCall("SymbolicLinkName '%!USTR!'", iface);
	return STATUS_SUCCESS;
}

} // namespace


PAGEABLE NTSTATUS pnp_start_device(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();

	if (is_fdo(vdev->type)) {
		auto status = irp_send_synchronously(vdev->devobj_lower, irp);
		if (NT_ERROR(status)) {
			return irp_done(irp, status);
		}
	}

	NTSTATUS status = STATUS_SUCCESS;

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
		vdev->DevicePowerState = PowerDeviceD0;
		SET_NEW_PNP_STATE(vdev, Started);

		POWER_STATE ps;
		ps.DeviceState = PowerDeviceD0;
		PoSetPowerState(vdev->Self, DevicePowerState, ps);

		Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t! started", vdev->type);
	}

	return irp_done(irp, status);
}
