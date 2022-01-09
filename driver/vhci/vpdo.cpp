#include "vpdo.h"
#include "trace.h"
#include "vpdo.tmh"

#include "vhci.h"
#include "usbreq.h"
#include "usbdsc.h"

namespace
{

PAGEABLE UCHAR to_dev_speed(usb_device_speed speed)
{
	PAGED_CODE();

	switch (speed) {
	case USB_SPEED_SUPER_PLUS:
	case USB_SPEED_SUPER:
		return UsbSuperSpeed;
	case USB_SPEED_HIGH:
	case USB_SPEED_WIRELESS:
		return UsbHighSpeed;
	case USB_SPEED_FULL:
		return UsbFullSpeed;
	case USB_SPEED_LOW:
	case USB_SPEED_UNKNOWN:
	default:
		return UsbLowSpeed;
	}
}

PAGEABLE void set_speed(USB_NODE_CONNECTION_INFORMATION_EX &ci, usb_device_speed speed, bool ex)
{
	PAGED_CODE();

	if (ex) {
		ci.Speed = to_dev_speed(speed);
	} else {
		auto &r = reinterpret_cast<USB_NODE_CONNECTION_INFORMATION&>(ci);
		static_assert(sizeof(r) == sizeof(ci));
		r.LowSpeed = speed == USB_SPEED_LOW;
	}
}

PAGEABLE auto copy_ep(int i, USB_ENDPOINT_DESCRIPTOR *d, void *data)
{
	PAGED_CODE();

	auto pi = static_cast<USB_PIPE_INFO*>(data) + i;

	RtlCopyMemory(&pi->EndpointDescriptor, d, sizeof(*d));
	pi->ScheduleOffset = 0; // TODO

	return false;
}

} // namespace


PAGEABLE NTSTATUS vpdo_select_config(vpdo_dev_t *vpdo, _URB_SELECT_CONFIGURATION *r)
{
	PAGED_CODE();

	if (vpdo->actconfig) {
		ExFreePoolWithTag(vpdo->actconfig, USBIP_VHCI_POOL_TAG);
		vpdo->actconfig = nullptr;
	}

	auto cd = r->ConfigurationDescriptor;
	if (!cd) {
		Trace(TRACE_LEVEL_INFORMATION, "Going to unconfigured state");
		vpdo->current_intf_num = 0;
		vpdo->current_intf_alt = 0;
		return STATUS_SUCCESS;
	}

	vpdo->actconfig = (USB_CONFIGURATION_DESCRIPTOR*)ExAllocatePoolWithTag(PagedPool, cd->wTotalLength, USBIP_VHCI_POOL_TAG);

	if (vpdo->actconfig) {
		RtlCopyMemory(vpdo->actconfig, cd, cd->wTotalLength);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Failed to allocate wTotalLength %d", cd->wTotalLength);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	NTSTATUS status = setup_config(r, vpdo->speed);

	if (NT_SUCCESS(status)) {
		r->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)(0x100 | cd->bConfigurationValue);

		char buf[SELECT_CONFIGURATION_STR_BUFSZ];
		Trace(TRACE_LEVEL_INFORMATION, "%s", select_configuration_str(buf, sizeof(buf), r));
	}

	return status;
}

PAGEABLE NTSTATUS vpdo_select_interface(vpdo_dev_t *vpdo, _URB_SELECT_INTERFACE *r)
{
	PAGED_CODE();

	if (!vpdo->actconfig) {
		Trace(TRACE_LEVEL_ERROR, "Device is unconfigured");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	auto &iface = r->Interface;
	auto status = setup_intf(&iface, vpdo->speed, vpdo->actconfig);

	if (NT_SUCCESS(status)) {
		char buf[SELECT_INTERFACE_STR_BUFSZ];
		Trace(TRACE_LEVEL_INFORMATION, "%s", select_interface_str(buf, sizeof(buf), r));

		vpdo->current_intf_num = iface.InterfaceNumber;
		vpdo->current_intf_alt = iface.AlternateSetting;
	}

	return status;
}

PAGEABLE NTSTATUS vpdo_get_nodeconn_info(vpdo_dev_t *vpdo, USB_NODE_CONNECTION_INFORMATION_EX &ci, ULONG *poutlen, bool ex)
{
	PAGED_CODE();

	NT_ASSERT(ci.ConnectionIndex);
	TraceCall("%p: ConnectionIndex %lu", vpdo, ci.ConnectionIndex); // input parameter

	RtlZeroMemory(&ci.DeviceDescriptor, sizeof(ci.DeviceDescriptor));
	ci.CurrentConfigurationValue = vpdo && vpdo->actconfig ? vpdo->actconfig->bConfigurationValue : 0;
	set_speed(ci, vpdo ? vpdo->speed : USB_SPEED_UNKNOWN, ex);
	ci.DeviceIsHub = FALSE;
	ci.DeviceAddress = static_cast<USHORT>(ci.ConnectionIndex);
	ci.NumberOfOpenPipes = 0;
	ci.ConnectionStatus = vpdo ? DeviceConnected : NoDeviceConnected;

	if (!vpdo) {
		*poutlen = sizeof(ci);
		return STATUS_SUCCESS;
	}

	if (is_valid_dsc(&vpdo->descriptor)) {
		RtlCopyMemory(&ci.DeviceDescriptor, &vpdo->descriptor, sizeof(ci.DeviceDescriptor));
	} else {
		Trace(TRACE_LEVEL_ERROR, "Device descriptor is not initialized");
		return STATUS_INVALID_PARAMETER;
	}

	auto iface = vpdo->actconfig ? dsc_find_intf(vpdo->actconfig, vpdo->current_intf_num, vpdo->current_intf_alt) : nullptr;
	if (iface) {
		ci.NumberOfOpenPipes = iface->bNumEndpoints;
	}

	ULONG outlen = sizeof(ci) + ci.NumberOfOpenPipes*sizeof(*ci.PipeList);
	NTSTATUS status = STATUS_SUCCESS;

	if (*poutlen < outlen) {
		status = STATUS_BUFFER_TOO_SMALL;
	} else if (ci.NumberOfOpenPipes > 0) {
		dsc_for_each_endpoint(vpdo->actconfig, iface, copy_ep, ci.PipeList);
	}

	*poutlen = outlen;
	return status;
}

PAGEABLE NTSTATUS vpdo_get_nodeconn_info_ex_v2(vpdo_dev_t*, USB_NODE_CONNECTION_INFORMATION_EX_V2 &ci, ULONG *poutlen)
{
	PAGED_CODE();

	if (*poutlen != sizeof(ci)) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	if (ci.Length != sizeof(ci)) {
		return STATUS_INVALID_PARAMETER;
	}

	*poutlen = sizeof(ci);

	ci.SupportedUsbProtocols.ul = 0;
	ci.SupportedUsbProtocols.Usb110 = true;
	ci.SupportedUsbProtocols.Usb200 = true;
//	ci.SupportedUsbProtocols.Usb300 = true;

	ci.Flags.ul = 0;
/*
	ci.Flags.DeviceIsOperatingAtSuperSpeedOrHigher = false;
	ci.Flags.DeviceIsSuperSpeedCapableOrHigher = false;
	ci.Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher = false;
	ci.Flags.DeviceIsSuperSpeedPlusCapableOrHigher = false;
*/

	return STATUS_SUCCESS;
}
