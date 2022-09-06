#include "ioctl_vhub.h"
#include <libdrv\dbgcommon.h>
#include "trace.h"
#include "ioctl_vhub.tmh"

#include <libdrv\ch11.h>

#include "ioctl_vhci.h"
#include "vhci.h"
#include "pnp.h"
#include "vhub.h"
#include "internal_ioctl.h"

namespace
{

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE USB_COMMON_DESCRIPTOR *find_descriptor(USB_CONFIGURATION_DESCRIPTOR *cd, UCHAR type, UCHAR index)
{
	PAGED_CODE();

	USB_COMMON_DESCRIPTOR *from{};
	auto end = reinterpret_cast<char*>(cd + cd->wTotalLength);

	for (int i = 0; (char*)from < end; ++i) {
		from = dsc_find_next(cd, from, type);
		if (!from) {
			break;
		}
		if (i == index) {
			NT_ASSERT(from->bDescriptorType == type);
			return from;
		}
	}

	return nullptr;
}

/*
 * USB_REQUEST_GET_DESCRIPTOR must not be sent to a server.
 * This IRP_MJ_DEVICE_CONTROL request can run concurrently with IRP_MJ_INTERNAL_DEVICE_CONTROL requests. 
 * As a result it can modify the state of the device in the middle of the multi stage operation.
 * For example, SCSI protocol (usb flash drives, etc.) issues three bulk requests: 
 * control block 31 bytes, data ??? bytes, status block 13 bytes.
 * Another request between any of these will break the state of the device and it will stop working.
 * 
 * FIXME: how to get not cached descriptors?
 */
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS do_get_descr_from_nodeconn(_In_ vpdo_dev_t *vpdo, _Inout_ USB_DESCRIPTOR_REQUEST &r, _Out_ ULONG &outlen)
{
	PAGED_CODE();

	auto setup = (USB_DEFAULT_PIPE_SETUP_PACKET*)&r.SetupPacket;
	static_assert(sizeof(*setup) == sizeof(r.SetupPacket));

	auto cfg = vpdo->actconfig ? vpdo->actconfig->bConfigurationValue : 0;

	auto type  = setup->wValue.HiByte;
	auto index = setup->wValue.LowByte;
//	auto lang_id = setup->wIndex.W; // for string descriptor

	void *dsc_data{};
	USHORT dsc_len = 0;

	switch (type) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		dsc_data = &vpdo->descriptor;
		dsc_len = vpdo->descriptor.bLength;
		break;
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		if (cfg > 0 && cfg - 1 == index) { // FIXME: can be wrong assumption
			dsc_data = vpdo->actconfig;
			dsc_len = vpdo->actconfig->wTotalLength;
		} else {
			TraceDbg("bConfigurationValue(%d) - 1 != Index(%d)", cfg, index);
		}
		break;
	case USB_STRING_DESCRIPTOR_TYPE: // lang_id is ignored
		if (index < ARRAYSIZE(vpdo->strings)) {
			if (volatile auto &d = vpdo->strings[index]) {
				dsc_len = d->bLength;
				dsc_data = d;
			}
		}
		break;
	case USB_INTERFACE_DESCRIPTOR_TYPE:
	case USB_ENDPOINT_DESCRIPTOR_TYPE:
		if (auto cd = vpdo->actconfig) {
			if (auto d = find_descriptor(cd, type, index)) {
				dsc_len = d->bLength;
				dsc_data = d;
			}
		}
		break;
	}

	NT_ASSERT(outlen > sizeof(r));
	auto TransferBufferLength = outlen - ULONG(sizeof(r)); // r.Data[]

	if (!dsc_data) {
		TraceDbg("Precached %!usb_descriptor_type! not found", type);
		return STATUS_INSUFFICIENT_RESOURCES; // can't send USB_REQUEST_GET_DESCRIPTOR
	}

	auto cnt = min(dsc_len, TransferBufferLength);
	RtlCopyMemory(r.Data, dsc_data, cnt);
	outlen = sizeof(r) + cnt;

	TraceDbg("%lu bytes%!BIN!", cnt, WppBinary(dsc_data, USHORT(cnt)));
	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto to_dev_speed(_In_ usb_device_speed speed)
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

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void set_speed(_Inout_ USB_NODE_CONNECTION_INFORMATION_EX &ci, _In_ usb_device_speed speed, _In_ bool ex)
{
	PAGED_CODE();

	if (ex) {
		ci.Speed = static_cast<UCHAR>(to_dev_speed(speed));
	} else {
		auto &r = reinterpret_cast<USB_NODE_CONNECTION_INFORMATION&>(ci);
		static_assert(sizeof(r) == sizeof(ci));
		r.LowSpeed = speed == USB_SPEED_LOW;
	}
}

_IRQL_requires_(PASSIVE_LEVEL)
_Function_class_(for_each_ep_fn)
PAGEABLE NTSTATUS copy_endpoint(_In_ int i, _In_ const USB_ENDPOINT_DESCRIPTOR &d, _In_ void *data)
{
	PAGED_CODE();

	auto &r = *static_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(data);

	if (ULONG(i) < r.NumberOfOpenPipes) {
		auto &p = r.PipeList[i];
		RtlCopyMemory(&p.EndpointDescriptor, &d, sizeof(d));
		p.ScheduleOffset = 0; // FIXME: TODO
		return STATUS_SUCCESS;
	} else {
		Trace(TRACE_LEVEL_ERROR, "Endpoint index %d, NumberOfOpenPipes %lu, ", i, r.NumberOfOpenPipes);
		return STATUS_INVALID_PARAMETER;
	}
}

/*
 * @param vpdo NULL if device is not plugged into the port
 */
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_nodeconn_info(
	_In_ vpdo_dev_t *vpdo, _Inout_ USB_NODE_CONNECTION_INFORMATION_EX &ci, _Out_ ULONG &outlen, _In_ bool ex)
{
	PAGED_CODE();

	TraceMsg("ConnectionIndex %lu, vpdo %04x", ci.ConnectionIndex, ptr4log(vpdo)); // input parameter

	NT_ASSERT(outlen >= sizeof(ci));
	auto old_outlen = outlen;
	outlen = sizeof(ci);

	RtlZeroMemory(&ci.DeviceDescriptor, sizeof(ci.DeviceDescriptor));
	ci.CurrentConfigurationValue = vpdo && vpdo->actconfig ? vpdo->actconfig->bConfigurationValue : 0;
	set_speed(ci, vpdo ? vpdo->speed : USB_SPEED_UNKNOWN, ex);
	ci.DeviceIsHub = false;
	ci.DeviceAddress = vpdo ? static_cast<USHORT>(make_vport(vpdo->version, vpdo->port)) : 0;
	ci.NumberOfOpenPipes = 0;
	ci.ConnectionStatus = vpdo ? DeviceConnected : NoDeviceConnected;

	if (!vpdo) {
		return STATUS_SUCCESS;
	}

	RtlCopyMemory(&ci.DeviceDescriptor, &vpdo->descriptor, sizeof(ci.DeviceDescriptor));

	auto iface = vpdo->actconfig ? dsc_find_intf(vpdo->actconfig, vpdo->current_intf_num, vpdo->current_intf_alt) : nullptr;
	if (iface) {
		ci.NumberOfOpenPipes = iface->bNumEndpoints;
	}

	if (old_outlen == outlen) { // header only requested
		return STATUS_SUCCESS;
	}

	ULONG pipes_sz = ci.NumberOfOpenPipes*sizeof(*ci.PipeList);
	ULONG full_sz = outlen + pipes_sz;

	outlen = full_sz;

	if (old_outlen < full_sz) {
		Trace(TRACE_LEVEL_ERROR, "NumberOfOpenPipes %lu, outlen %lu < %lu", ci.NumberOfOpenPipes, old_outlen, full_sz);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (ci.NumberOfOpenPipes) {
		RtlZeroMemory(ci.PipeList, pipes_sz);
		return for_each_endpoint(vpdo->actconfig, iface, copy_endpoint, &ci);
	}

	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto get_nodeconn_info(_In_ vhub_dev_t &vhub, _In_ void *buffer, _In_ ULONG inlen, _Out_ ULONG &outlen, _In_ bool ex)
{
	PAGED_CODE();

	static_assert(sizeof(USB_NODE_CONNECTION_INFORMATION) == sizeof(USB_NODE_CONNECTION_INFORMATION_EX));
	auto &ci = *reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(buffer);

	if (inlen < sizeof(ci) || outlen < sizeof(ci)) {
		outlen = sizeof(ci);
		return STATUS_BUFFER_TOO_SMALL;
	}

	NT_ASSERT(ci.ConnectionIndex);
	auto vpdo = vhub_find_vpdo(vhub, ci.ConnectionIndex); // NULL if port is empty

	return get_nodeconn_info(vpdo, ci, outlen, ex);
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_nodeconn_info_ex_v2(
	_In_ vhub_dev_t &vhub, _Out_ USB_NODE_CONNECTION_INFORMATION_EX_V2 &ci, _In_ ULONG inlen, _Out_ ULONG &outlen)
{
	PAGED_CODE();

	if (!(inlen == sizeof(ci) && outlen == sizeof(ci))) {
		outlen = sizeof(ci);
		return STATUS_INVALID_BUFFER_SIZE;
	}

	if (ci.Length != sizeof(ci)) {
		return STATUS_INVALID_PARAMETER;
	}

	NT_ASSERT(ci.ConnectionIndex);
	TraceMsg("%!hci_version!, ConnectionIndex %lu", vhub.version, ci.ConnectionIndex);

	switch (vhub.version) {
	case HCI_USB3:
		ci.SupportedUsbProtocols.Usb110 = false;
		ci.SupportedUsbProtocols.Usb200 = false;
		ci.SupportedUsbProtocols.Usb300 = true;
		break;
	case HCI_USB2:
		ci.SupportedUsbProtocols.Usb110 = true;
		ci.SupportedUsbProtocols.Usb200 = true;
		ci.SupportedUsbProtocols.Usb300 = false;
		break;
	}

	if (auto vpdo = vhub_find_vpdo(vhub, ci.ConnectionIndex)) {
		switch (vpdo->speed) {
		case USB_SPEED_SUPER_PLUS:
			ci.Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher = true;
			ci.Flags.DeviceIsSuperSpeedPlusCapableOrHigher = true;
			break;
		case USB_SPEED_SUPER:
			ci.Flags.DeviceIsOperatingAtSuperSpeedOrHigher = true;
			ci.Flags.DeviceIsSuperSpeedCapableOrHigher = true;
			break;
		}
	} else {
		ci.Flags.ul = 0;
	}

	return STATUS_SUCCESS;
}

/*
 * See: <linux>/drivers/usb/usbip/vhci_hcd.c, hub_descriptor
 */
PAGEABLE void get_hub_descriptor(_In_ vhub_dev_t &vhub, _Out_ USB_HUB_DESCRIPTOR &d)
{
	PAGED_CODE();

	static_assert(vhub.NUM_PORTS <= USB_MAXCHILDREN);
	constexpr auto width = vhub.NUM_PORTS/8 + 1;

	d.bDescriptorLength = USB_DT_HUB_NONVAR_SIZE + 2*width;
	d.bDescriptorType = USB_20_HUB_DESCRIPTOR_TYPE; // USB_DT_HUB
	d.bNumberOfPorts = vhub.NUM_PORTS; 
	d.wHubCharacteristics = HUB_CHAR_INDV_PORT_LPSM | HUB_CHAR_COMMON_OCPM;
	d.bPowerOnToPowerGood = 0;
	d.bHubControlCurrent = 0;

	RtlZeroMemory(d.bRemoveAndPowerMask, width);
	RtlFillMemory(d.bRemoveAndPowerMask + width, width, UCHAR(-1));
}

/*
* See: <linux>/drivers/usb/usbip/vhci_hcd.c, ss_hub_descriptor
*/
inline PAGEABLE void get_hub_descriptor(_In_ vhub_dev_t &vhub, _Out_ USB_30_HUB_DESCRIPTOR &d)
{
	PAGED_CODE();

	d.bLength = USB_DT_SS_HUB_SIZE;
	d.bDescriptorType = USB_30_HUB_DESCRIPTOR_TYPE; // USB_DT_SS_HUB
	d.bNumberOfPorts = vhub.NUM_PORTS; 
	d.wHubCharacteristics = HUB_CHAR_INDV_PORT_LPSM | HUB_CHAR_COMMON_OCPM;
	d.bPowerOnToPowerGood = 0;
	d.bHubControlCurrent = 0;
	d.bHubHdrDecLat = 0x04; // worst case: 0.4 micro sec
	d.wHubDelay = 0; // The average delay, in nanoseconds, that is introduced by the hub
	d.DeviceRemovable = USHORT(-1); // Indicates whether a removable device is attached to each port
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_node_info(_In_ vhub_dev_t &vhub, _Out_ USB_NODE_INFORMATION &r, _In_ ULONG inlen, _Out_ ULONG &outlen)
{
	PAGED_CODE();

	if (!(inlen == sizeof(r) && outlen == sizeof(r))) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	switch (r.NodeType) {
	case UsbHub: {
		auto &h = r.u.HubInformation;
		get_hub_descriptor(vhub, h.HubDescriptor);
		h.HubIsBusPowered = false;
		TraceMsg("%!hci_version!, UsbHub, bNumberOfPorts %d", vhub.version, h.HubDescriptor.bNumberOfPorts);
	} break;
	case UsbMIParent: {
		auto &p = r.u.MiParentInformation;
		p.NumberOfInterfaces = 1; // FIXME
		TraceMsg("%!hci_version!, UsbMIParent, NumberOfInterfaces %lu", vhub.version, p.NumberOfInterfaces);
	} break;
	}

	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_descr_from_nodeconn(vhub_dev_t &vhub, USB_DESCRIPTOR_REQUEST &r, ULONG inlen, ULONG &outlen)
{
	PAGED_CODE();

	auto &pkt = r.SetupPacket;

	UCHAR type = pkt.wValue >> 8;
	UCHAR idx  = pkt.wValue & 0xFF;

	USHORT lang_id = pkt.wIndex;

	TraceMsg("ConnectionIndex %lu, %!usb_descriptor_type!, index %d, lang_id %#x; inlen %lu, outlen %lu", 
		  r.ConnectionIndex, type, idx, lang_id, inlen, outlen);

	if (!(inlen >= sizeof(r) && outlen > sizeof(r))) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (auto vpdo = vhub_find_vpdo(vhub, r.ConnectionIndex)) {
		return do_get_descr_from_nodeconn(vpdo, r, outlen);
	}

	return STATUS_NO_SUCH_DEVICE;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_hub_information_ex(vhub_dev_t &vhub, USB_HUB_INFORMATION_EX &r, ULONG &outlen)
{
	PAGED_CODE();

	if (outlen == sizeof(r)) {
		return vhub_get_information_ex(vhub, r);
	}

	outlen = sizeof(r);
	return STATUS_INVALID_BUFFER_SIZE;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_hub_capabilities(vhub_dev_t &vhub, USB_HUB_CAPABILITIES &r, ULONG &outlen)
{
	PAGED_CODE();

	bool ok = outlen >= sizeof(r);
	outlen = sizeof(r);

	if (ok) {
		r.HubIs2xCapable = vhub.version == HCI_USB2;
	}

	return ok ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_hub_capabilities_ex(vhub_dev_t &vhub, USB_HUB_CAPABILITIES_EX &r, ULONG &outlen)
{
	PAGED_CODE();

	bool ok = outlen >= sizeof(r);
	outlen = sizeof(r);
	if (!ok) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &f = r.CapabilityFlags;
	f.ul = 0;

	auto usb2 = vhub.version == HCI_USB2;
	f.HubIsHighSpeedCapable = usb2;
	f.HubIsHighSpeed = usb2;

	f.HubIsMultiTtCapable = false;
	f.HubIsMultiTt = false;

	f.HubIsRoot = true;
//	f.HubIsArmedWakeOnConnect = true; // the hub is armed to wake when a device is connected to the hub
//	f.HubIsBusPowered = false;

	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_port_connector_properties(vhub_dev_t &vhub, USB_PORT_CONNECTOR_PROPERTIES &r, ULONG inlen, ULONG &outlen)
{
	PAGED_CODE();

	if (inlen >= sizeof(r)) {
		return vhub_get_port_connector_properties(vhub, r, outlen);
	}

	outlen = sizeof(r);
	return STATUS_BUFFER_TOO_SMALL;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_node_driverkey_name(vhub_dev_t &vhub, USB_NODE_CONNECTION_DRIVERKEY_NAME &r, ULONG inlen, ULONG &outlen)
{
	PAGED_CODE();

	if (inlen < sizeof(r)) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto vpdo = vhub_find_vpdo(vhub, r.ConnectionIndex);
	if (!vpdo) {
		return STATUS_NO_SUCH_DEVICE;
	}

	NTSTATUS status{};
	ULONG driverkeylen = 0;

	auto driverkey = (PWSTR)GetDeviceProperty(vpdo->Self, DevicePropertyDriverKeyName, status, driverkeylen);
	if (!driverkey) {
		return status;
	}

	ULONG outlen_res = sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME) + driverkeylen - sizeof(WCHAR);

	if (outlen >= sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME)) {
		r.ActualLength = outlen_res;
		if (outlen >= outlen_res) {
			RtlCopyMemory(r.DriverKeyName, driverkey, driverkeylen);
			outlen = outlen_res;
		} else {
			RtlCopyMemory(r.DriverKeyName, driverkey, outlen - sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME) + sizeof(WCHAR));
		}
	} else {
		status = STATUS_INSUFFICIENT_RESOURCES;
		outlen = outlen_res;
	}

	ExFreePoolWithTag(driverkey, USBIP_VHCI_POOL_TAG);
	return status;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_node_connection_attributes(vhub_dev_t &vhub, USB_NODE_CONNECTION_ATTRIBUTES &r, ULONG inlen, ULONG &outlen)
{
	PAGED_CODE();

	if (!(inlen == sizeof(r) && outlen == sizeof(r))) {
		outlen = sizeof(r);
		return STATUS_INVALID_BUFFER_SIZE;
	}

	NT_ASSERT(r.ConnectionIndex);
	TraceMsg("ConnectionIndex %lu", r.ConnectionIndex);

	auto vpdo = vhub_find_vpdo(vhub, r.ConnectionIndex);

	r.ConnectionStatus = vpdo ? DeviceConnected : NoDeviceConnected;
	r.PortAttributes = USB_PORTATTR_NO_OVERCURRENT_UI;
	
	return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS vhci_ioctl_vhub(_Inout_ vhub_dev_t &vhub, _In_ ULONG ioctl_code, _Inout_ void *buffer, _In_ ULONG inlen, _Inout_ ULONG &outlen)
{
	PAGED_CODE();

	auto status = STATUS_INVALID_DEVICE_REQUEST;

	switch (ioctl_code) {
	case IOCTL_USB_GET_NODE_INFORMATION:
		status = get_node_info(vhub, *static_cast<USB_NODE_INFORMATION*>(buffer), inlen, outlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION:
		status = get_nodeconn_info(vhub, buffer, inlen, outlen, false);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX:
		status = get_nodeconn_info(vhub, buffer, inlen, outlen, true);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2:
		status = get_nodeconn_info_ex_v2(vhub, *reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX_V2*>(buffer), inlen, outlen);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		status = get_descr_from_nodeconn(vhub, *static_cast<USB_DESCRIPTOR_REQUEST*>(buffer), inlen, outlen);
		break;
	case IOCTL_USB_GET_HUB_INFORMATION_EX:
		status = get_hub_information_ex(vhub, *static_cast<USB_HUB_INFORMATION_EX*>(buffer), outlen);
		break;
	case IOCTL_USB_GET_HUB_CAPABILITIES:
		status = get_hub_capabilities(vhub, *static_cast<USB_HUB_CAPABILITIES*>(buffer), outlen);
		break;
	case IOCTL_USB_GET_HUB_CAPABILITIES_EX:
		status = get_hub_capabilities_ex(vhub, *static_cast<USB_HUB_CAPABILITIES_EX*>(buffer), outlen);
		break;
	case IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES:
		status = get_port_connector_properties(vhub, *static_cast<USB_PORT_CONNECTOR_PROPERTIES*>(buffer), inlen, outlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME:
		status = get_node_driverkey_name(vhub, *static_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(buffer), inlen, outlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_ATTRIBUTES:
		status = get_node_connection_attributes(vhub, *static_cast<USB_NODE_CONNECTION_ATTRIBUTES*>(buffer), inlen, outlen);
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "Unhandled %s(%#08lX)", device_control_name(ioctl_code), ioctl_code);
	}

	return status;
}
