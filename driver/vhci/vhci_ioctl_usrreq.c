#include "vhci_ioctl_usrreq.h"
#include "trace.h"
#include "vhci_ioctl_usrreq.tmh"

#include "vhci_dbg.h"

#include <usbdi.h>
#include <usbuser.h>

static PAGEABLE NTSTATUS
get_power_info(PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_POWER_INFO	pinfo = (PUSB_POWER_INFO)buffer;

	if (inlen < sizeof(USB_POWER_INFO))
		return STATUS_BUFFER_TOO_SMALL;

	pinfo->HcDeviceWake = WdmUsbPowerDeviceUnspecified;
	pinfo->HcSystemWake = WdmUsbPowerNotMapped;
	pinfo->RhDeviceWake = WdmUsbPowerDeviceD2;
	pinfo->RhSystemWake = WdmUsbPowerSystemWorking;
	pinfo->LastSystemSleepState = WdmUsbPowerNotMapped;

	switch (pinfo->SystemState) {
	case WdmUsbPowerSystemWorking:
		pinfo->HcDevicePowerState = WdmUsbPowerDeviceD0;
		pinfo->RhDevicePowerState = WdmUsbPowerNotMapped;
		break;
	case WdmUsbPowerSystemSleeping1:
	case WdmUsbPowerSystemSleeping2:
	case WdmUsbPowerSystemSleeping3:
		pinfo->HcDevicePowerState = WdmUsbPowerDeviceUnspecified;
		pinfo->RhDevicePowerState = WdmUsbPowerDeviceD3;
		break;
	case WdmUsbPowerSystemHibernate:
		pinfo->HcDevicePowerState = WdmUsbPowerDeviceD3;
		pinfo->RhDevicePowerState = WdmUsbPowerDeviceD3;
		break;
	case WdmUsbPowerSystemShutdown:
		pinfo->HcDevicePowerState = WdmUsbPowerNotMapped;
		pinfo->RhDevicePowerState = WdmUsbPowerNotMapped;
		break;
	}
	pinfo->CanWakeup = FALSE;
	pinfo->IsPowered = FALSE;

	*poutlen = sizeof(USB_POWER_INFO);

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
get_controller_info(PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_CONTROLLER_INFO_0	pinfo = (PUSB_CONTROLLER_INFO_0)buffer;

	if (inlen < sizeof(USB_CONTROLLER_INFO_0))
		return STATUS_BUFFER_TOO_SMALL;
	pinfo->PciVendorId = 0;
	pinfo->PciDeviceId = 0;
	pinfo->PciRevision = 0;
	pinfo->NumberOfRootPorts = 1;
	pinfo->ControllerFlavor = EHCI_Generic;
	pinfo->HcFeatureFlags = 0;

	*poutlen = sizeof(USB_CONTROLLER_INFO_0);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhci_ioctl_user_request(pvhci_dev_t vhci, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	USBUSER_REQUEST_HEADER	*hdr = (USBUSER_REQUEST_HEADER *)buffer;
	NTSTATUS	status = STATUS_INVALID_DEVICE_REQUEST;

	UNREFERENCED_PARAMETER(vhci);

	if (inlen < sizeof(USBUSER_REQUEST_HEADER)) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	TraceInfo(DBG_IOCTL, "usb user request: code: %s\n", dbg_usb_user_request_code(hdr->UsbUserRequest));

	buffer = (PVOID)(hdr + 1);
	inlen -= sizeof(USBUSER_REQUEST_HEADER);
	(*poutlen) -= sizeof(USBUSER_REQUEST_HEADER);

	switch (hdr->UsbUserRequest) {
	case USBUSER_GET_POWER_STATE_MAP:
		status = get_power_info(buffer, inlen, poutlen);
		break;
	case USBUSER_GET_CONTROLLER_INFO_0:
		status = get_controller_info(hdr + 1, inlen, poutlen);
		break;
	default:
		TraceInfo(DBG_IOCTL, "usb user request: unhandled code: %s\n", dbg_usb_user_request_code(hdr->UsbUserRequest));
		hdr->UsbUserStatusCode = UsbUserNotSupported;
		break;
	}

	if (NT_SUCCESS(status)) {
		(*poutlen) += sizeof(USBUSER_REQUEST_HEADER);
		hdr->UsbUserStatusCode = UsbUserSuccess;
		hdr->ActualBufferLength = *poutlen;
	}
	else {
		hdr->UsbUserStatusCode = UsbUserMiniportError;///TODO

	}
	return status;
}