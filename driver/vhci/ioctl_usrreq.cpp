#include "ioctl_usrreq.h"
#include "trace.h"
#include "ioctl_usrreq.tmh"

#include "dev.h"
#include "ioctl_vhci.h"

namespace
{

PAGEABLE NTSTATUS get_power_state_map(vhci_dev_t*, void *request, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto &r = *static_cast<USB_POWER_INFO*>(request);

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

PAGEABLE NTSTATUS get_controller_info(vhci_dev_t*, void *request, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto &r = *static_cast<USB_CONTROLLER_INFO_0*>(request);

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

PAGEABLE NTSTATUS get_usb_driver_version(vhci_dev_t*, void *request, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto &r = *static_cast<USB_DRIVER_VERSION_PARAMETERS*>(request);

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

PAGEABLE auto get_controller_driver_key(vhci_dev_t *vhci, void *request, ULONG, ULONG *poutlen)
{
	PAGED_CODE();

	auto &r = *static_cast<USB_UNICODE_NAME*>(request);
	auto &name = reinterpret_cast<USB_HCD_DRIVERKEY_NAME&>(r);

	static_assert(sizeof(r) == sizeof(name));
	static_assert(sizeof(r.Length) == sizeof(name.ActualLength));
	static_assert(sizeof(r.String) == sizeof(name.DriverKeyName));

	return get_hcd_driverkey_name(vhci, name, poutlen);
}

PAGEABLE auto get_roothub_symbolic_name(vhci_dev_t *vhci, void *request, ULONG, ULONG *poutlen)
{
	PAGED_CODE();

	auto &r = *static_cast<USB_UNICODE_NAME*>(request);
	auto &name = reinterpret_cast<USB_ROOT_HUB_NAME&>(r);

	static_assert(sizeof(r) == sizeof(name));
	static_assert(sizeof(r.Length) == sizeof(name.ActualLength));
	static_assert(sizeof(r.String) == sizeof(name.RootHubName));

	auto vhub = vhub_from_vhci(vhci);
	return vhub_get_roothub_name(vhub, name, poutlen);
}

PAGEABLE auto get_device_count(const vhub_dev_t *vhub)
{
	int cnt = 0;

	for (auto d: vhub->vpdo) {
		if (d) {
			++cnt;
		}
	}

	return cnt;
}

PAGEABLE auto get_bandwidth_information(vhci_dev_t *vhci, void *request, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto &r = *static_cast<USB_BANDWIDTH_INFO*>(request);

	*poutlen = sizeof(r);
	if (inlen != sizeof(r)) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	RtlZeroMemory(&r, sizeof(r));

	auto vhub = vhub_from_vhci(vhci);
	r.DeviceCount = get_device_count(vhub);

	return STATUS_SUCCESS;
}

PAGEABLE auto GetCurrentSystemTime()
{
	LARGE_INTEGER CurrentTime;
	KeQuerySystemTimePrecise(&CurrentTime);
	return CurrentTime;
}

PAGEABLE auto get_bus_statistics_0(vhci_dev_t *vhci, void *request, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto &r = *static_cast<USB_BUS_STATISTICS_0*>(request);

	*poutlen = sizeof(r);
	if (inlen != sizeof(r)) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	RtlZeroMemory(&r, sizeof(r));

	auto vhub = vhub_from_vhci(vhci);
	r.DeviceCount = get_device_count(vhub);

	r.CurrentSystemTime = GetCurrentSystemTime();

/*
	r.CurrentUsbFrame;
	r.BulkBytes;
	r.IsoBytes;
	r.InterruptBytes;
	r.ControlDataBytes;
	r.PciInterruptCount;
	r.HardResetCount;
	r.WorkerSignalCount;
	r.CommonBufferBytes;
	r.WorkerIdleTimeMs;
*/
	r.RootHubEnabled = true;

	r.RootHubDevicePowerState = vhub->DevicePowerState != PowerDeviceUnspecified ?
					static_cast<UCHAR>(vhub->DevicePowerState) - 1 : 0;

//	r.Unused;
//	r.NameIndex;
	
	return STATUS_SUCCESS;
}

PAGEABLE auto get_usb2_hw_version(vhci_dev_t*, void *data, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto &r = *static_cast<USB_USB2HW_VERSION_PARAMETERS*>(data);
	*poutlen = sizeof(r);

	if (inlen == sizeof(r)) {
		r.Usb2HwRevision = 0; // USB2HW_UNKNOWN
		return STATUS_SUCCESS;
	}

	return STATUS_INVALID_BUFFER_SIZE;
}

PAGEABLE auto usb_refresh_hct_reg(vhci_dev_t*, void *data, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto &r = *static_cast<USBUSER_REFRESH_HCT_REG*>(data);
	*poutlen = sizeof(r);

	return inlen == sizeof(r) ? STATUS_NOT_SUPPORTED : STATUS_INVALID_BUFFER_SIZE;
}

using request_t = NTSTATUS(vhci_dev_t*, void *buffer, ULONG inlen, ULONG *poutlen);

/*
 * The following APIS are enabled always
 */
request_t* const requests[] =
{
	nullptr,
	get_controller_info,
	get_controller_driver_key,
	nullptr, // USBUSER_PASS_THRU
	get_power_state_map,
	get_bandwidth_information,
	get_bus_statistics_0,
	get_roothub_symbolic_name,
	get_usb_driver_version,
	get_usb2_hw_version,
	usb_refresh_hct_reg
};

} // namespace


PAGEABLE NTSTATUS vhci_ioctl_user_request(vhci_dev_t *vhci, USBUSER_REQUEST_HEADER *hdr, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	if (inlen < sizeof(*hdr)) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	inlen -= sizeof(*hdr);
	*poutlen -= sizeof(*hdr);

	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
	auto req = hdr->UsbUserRequest;

	if (auto f = req < ARRAYSIZE(requests) ? requests[req] : nullptr) {
		status = f(vhci, hdr + 1, inlen, poutlen);
	} else {
		Trace(TRACE_LEVEL_WARNING, "Unhandled %!usbuser!", req);
		hdr->UsbUserStatusCode = req ? UsbUserNotSupported : UsbUserInvalidRequestCode;
	}

	*poutlen += sizeof(*hdr);

	if (NT_SUCCESS(status)) {
		hdr->ActualBufferLength = *poutlen;
		hdr->UsbUserStatusCode = UsbUserSuccess;
	} else {
		hdr->UsbUserStatusCode = UsbUserMiniportError;

	}

	TraceCall("%!usbuser! -> %!USB_USER_ERROR_CODE!, %!STATUS!", hdr->UsbUserRequest, hdr->UsbUserStatusCode, status);
	return status;
}
