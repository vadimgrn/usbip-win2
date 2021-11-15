#include "vhci_pnp_start.h"
#include "trace.h"
#include "vhci_pnp_start.tmh"

#include "vhci_dbg.h"
#include "vhci_pnp.h"
#include "vhci_irp.h"
#include "vhci_wmi.h"
#include "usbip_vhci_api.h"

static PAGEABLE NTSTATUS
start_vhci(pvhci_dev_t vhci)
{
	NTSTATUS	status;

	PAGED_CODE();

	status = IoRegisterDeviceInterface(vhci->common.pdo, (LPGUID)&GUID_DEVINTERFACE_VHCI_USBIP, NULL, &vhci->DevIntfVhci);
	if (!NT_SUCCESS(status)) {
		TraceError(TRACE_PNP, "failed to register vhci device interface: %!STATUS!\n", status);
		return status;
	}
	status = IoRegisterDeviceInterface(vhci->common.pdo, (LPGUID)&GUID_DEVINTERFACE_USB_HOST_CONTROLLER, NULL, &vhci->DevIntfUSBHC);
	if (!NT_SUCCESS(status)) {
		TraceError(TRACE_PNP, "failed to register USB Host controller device interface: %!STATUS!\n", status);
		return status;
	}

	// Register with WMI
	status = reg_wmi(vhci);
	if (!NT_SUCCESS(status)) {
		TraceError(TRACE_VHCI, "reg_wmi failed: %!STATUS!\n", status);
	}

	return status;
}

static PAGEABLE NTSTATUS
start_vhub(pvhub_dev_t vhub)
{
	pvhci_dev_t	vhci;
	NTSTATUS	status;

	PAGED_CODE();

	status = IoRegisterDeviceInterface(vhub->common.pdo, (LPGUID)&GUID_DEVINTERFACE_USB_HUB, NULL, &vhub->DevIntfRootHub);
	if (NT_ERROR(status)) {
		TraceError(TRACE_PNP, "failed to register USB root hub device interface: %!STATUS!\n", status);
		return STATUS_UNSUCCESSFUL;
	}
	status = IoSetDeviceInterfaceState(&vhub->DevIntfRootHub, TRUE);
	if (NT_ERROR(status)) {
		TraceError(TRACE_PNP, "failed to activate USB root hub device interface: %!STATUS!\n", status);
		return STATUS_UNSUCCESSFUL;
	}

	vhci = (pvhci_dev_t)vhub->common.parent;
	status = IoSetDeviceInterfaceState(&vhci->DevIntfVhci, TRUE);
	if (!NT_SUCCESS(status)) {
		TraceError(TRACE_PNP, "failed to enable vhci device interface: %!STATUS!\n", status);
		return status;
	}
	status = IoSetDeviceInterfaceState(&vhci->DevIntfUSBHC, TRUE);
	if (!NT_SUCCESS(status)) {
		IoSetDeviceInterfaceState(&vhci->DevIntfVhci, FALSE);
		TraceError(TRACE_PNP, "failed to enable USB host controller device interface: %!STATUS!\n", status);
		return status;
	}
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
start_vpdo(pvpdo_dev_t vpdo)
{
	NTSTATUS	status;

	PAGED_CODE();

	status = IoRegisterDeviceInterface(TO_DEVOBJ(vpdo), &GUID_DEVINTERFACE_USB_DEVICE, NULL, &vpdo->usb_dev_interface);
	if (NT_SUCCESS(status)) {
		status = IoSetDeviceInterfaceState(&vpdo->usb_dev_interface, TRUE);
		if (NT_ERROR(status)) {
			TraceWarning(TRACE_VPDO, "failed to activate USB device interface: %!STATUS!\n", status);
		}
	}
	else {
		TraceWarning(TRACE_VPDO, "failed to register USB device interface: %!STATUS!\n", status);
	}

	return status;
}

PAGEABLE NTSTATUS
pnp_start_device(pvdev_t vdev, PIRP irp)
{
	NTSTATUS	status;

	if (IS_FDO(vdev->type)) {
		status = irp_send_synchronously(vdev->devobj_lower, irp);
		if (NT_ERROR(status)) {
			return irp_done(irp, status);
		}
	}
	switch (vdev->type) {
	case VDEV_VHCI:
		status = start_vhci((pvhci_dev_t)vdev);
		break;
	case VDEV_VHUB:
		status = start_vhub((pvhub_dev_t)vdev);
		break;
	case VDEV_VPDO:
		status = start_vpdo((pvpdo_dev_t)vdev);
		break;
	default:
		status = STATUS_SUCCESS;
		break;
	}

	if (NT_SUCCESS(status)) {
		POWER_STATE	powerState;

		vdev->DevicePowerState = PowerDeviceD0;
		SET_NEW_PNP_STATE(vdev, Started);
		powerState.DeviceState = PowerDeviceD0;
		PoSetPowerState(vdev->Self, DevicePowerState, powerState);

		TraceInfo(TRACE_GENERAL, "device(%!vdev_type_t!) started\n", vdev->type);
	}
	status = irp_done(irp, status);
	return status;
}