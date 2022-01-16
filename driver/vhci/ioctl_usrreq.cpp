#include "ioctl_usrreq.h"
#include "trace.h"
#include "ioctl_usrreq.tmh"

#include "ioctl_vhci.h"

#include <usbdi.h>
#include <usbuser.h>

namespace
{

PAGEABLE NTSTATUS get_power_info(USB_POWER_INFO &r, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	*poutlen = sizeof(r);
	if (inlen != sizeof(r)) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	r.HcDeviceWake = WdmUsbPowerDeviceUnspecified;
	r.HcSystemWake = WdmUsbPowerNotMapped;
	r.RhDeviceWake = WdmUsbPowerDeviceD2;
	r.RhSystemWake = WdmUsbPowerSystemWorking;
	r.LastSystemSleepState = WdmUsbPowerNotMapped;

	switch (r.SystemState) {
	case WdmUsbPowerSystemWorking:
		r.HcDevicePowerState = WdmUsbPowerDeviceD0;
		r.RhDevicePowerState = WdmUsbPowerNotMapped;
		break;
	case WdmUsbPowerSystemSleeping1:
	case WdmUsbPowerSystemSleeping2:
	case WdmUsbPowerSystemSleeping3:
		r.HcDevicePowerState = WdmUsbPowerDeviceUnspecified;
		r.RhDevicePowerState = WdmUsbPowerDeviceD3;
		break;
	case WdmUsbPowerSystemHibernate:
		r.HcDevicePowerState = WdmUsbPowerDeviceD3;
		r.RhDevicePowerState = WdmUsbPowerDeviceD3;
		break;
	case WdmUsbPowerSystemShutdown:
		r.HcDevicePowerState = WdmUsbPowerNotMapped;
		r.RhDevicePowerState = WdmUsbPowerNotMapped;
		break;
	}

	r.CanWakeup = FALSE;
	r.IsPowered = FALSE;

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS get_controller_info(USB_CONTROLLER_INFO_0 &r, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	*poutlen = sizeof(r);
	if (inlen != sizeof(r)) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	r.PciVendorId = 0x1D6B;
	r.PciDeviceId = 0x03;
	r.PciRevision = 0x513; // bcdDevice
	r.NumberOfRootPorts = 1;
	r.ControllerFlavor = USB_HcGeneric;
	r.HcFeatureFlags = 0;

	return STATUS_SUCCESS;
}

PAGEABLE get_usb_driver_version(USB_DRIVER_VERSION_PARAMETERS &r, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	*poutlen = sizeof(r);
	if (inlen != sizeof(r)) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	r.DriverTrackingCode = 0x04; // FIXME: A tracking code that identifies the revision of the USB stack
	r.USBDI_Version = USBDI_VERSION;
	r.USBUSER_Version = USBUSER_VERSION;
	r.CheckedPortDriver = false;
	r.CheckedMiniportDriver = false;
	r.USB_Version = 0; // BCD usb version 0x0110 (1.1) 0x0200 (2.0)

	TraceCall("USBDI_Version %#04lx, USB_Version %04lx", r.USBDI_Version, r.USB_Version);
	return STATUS_SUCCESS;
}

PAGEABLE auto get_controller_driver_key(vhci_dev_t *vhci, USB_UNICODE_NAME &r, ULONG *poutlen)
{
	PAGED_CODE();

	auto &name = reinterpret_cast<USB_HCD_DRIVERKEY_NAME&>(r);

	static_assert(sizeof(r) == sizeof(name));
	static_assert(sizeof(r.Length) == sizeof(name.ActualLength));
	static_assert(sizeof(r.String) == sizeof(name.DriverKeyName));

	TraceCall("outlen %lu", *poutlen);
	return get_hcd_driverkey_name(vhci, name, poutlen);
}

} // namespace


PAGEABLE NTSTATUS vhci_ioctl_user_request(vhci_dev_t *vhci, void *buffer, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto hdr = static_cast<USBUSER_REQUEST_HEADER*>(buffer);
	if (inlen < sizeof(*hdr)) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	TraceCall("%!usbuser!", hdr->UsbUserRequest);

	buffer = hdr + 1;
	inlen -= sizeof(*hdr);
	*poutlen -= sizeof(*hdr);

	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

	switch (hdr->UsbUserRequest) {
	case USBUSER_GET_POWER_STATE_MAP:
		status = get_power_info(*reinterpret_cast<USB_POWER_INFO*>(buffer), inlen, poutlen);
		break;
	case USBUSER_GET_CONTROLLER_INFO_0:
		status = get_controller_info(*reinterpret_cast<USB_CONTROLLER_INFO_0*>(hdr + 1), inlen, poutlen);
		break;
	case USBUSER_GET_USB_DRIVER_VERSION:
		status = get_usb_driver_version(*reinterpret_cast<USB_DRIVER_VERSION_PARAMETERS*>(hdr + 1), inlen, poutlen);
		break;
	case USBUSER_GET_CONTROLLER_DRIVER_KEY:
		status = get_controller_driver_key(vhci, *reinterpret_cast<USB_UNICODE_NAME*>(hdr + 1), poutlen);
		break;
	default:
		Trace(TRACE_LEVEL_WARNING, "Unhandled %!usbuser!", hdr->UsbUserRequest);
		hdr->UsbUserStatusCode = UsbUserNotSupported;
	}

	if (NT_SUCCESS(status)) {
		*poutlen += sizeof(*hdr);
		hdr->UsbUserStatusCode = UsbUserSuccess;
		hdr->ActualBufferLength = *poutlen;
	} else {
		hdr->UsbUserStatusCode = UsbUserMiniportError; // TODO

	}

	return status;
}
