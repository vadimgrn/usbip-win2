#include "ioctl_vhub.h"
#include "dbgcommon.h"
#include "trace.h"
#include "ioctl_vhub.tmh"

#include "ioctl_vhci.h"
#include "vhci.h"
#include "pnp.h"
#include "vpdo.h"
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
PAGEABLE NTSTATUS do_get_descr_from_nodeconn(vpdo_dev_t *vpdo, USB_DESCRIPTOR_REQUEST &r, ULONG &outlen)
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
		if (index < vpdo->strings_cnt) {
			if (auto d = vpdo->strings[index]) {
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

	TraceDbg("%lu bytes%!BIN!", cnt, WppBinary(dsc_data, (USHORT)cnt));
	return STATUS_SUCCESS;
}

_IRQL_requires_(LOW_LEVEL)
PAGEABLE auto get_nodeconn_info(vhub_dev_t *vhub, void *buffer, ULONG inlen, ULONG &outlen, bool ex)
{
	PAGED_CODE();

	static_assert(sizeof(USB_NODE_CONNECTION_INFORMATION) == sizeof(USB_NODE_CONNECTION_INFORMATION_EX));
	auto &ci = *reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(buffer);

	if (inlen < sizeof(ci) || outlen < sizeof(ci)) {
		outlen = sizeof(ci);
		return STATUS_BUFFER_TOO_SMALL;
	}

	NT_ASSERT(ci.ConnectionIndex);
	auto vpdo = vhub_find_vpdo(vhub, ci.ConnectionIndex);

	return vpdo_get_nodeconn_info(vpdo, ci, outlen, ex);
}

_IRQL_requires_(LOW_LEVEL)
PAGEABLE NTSTATUS get_node_info(vhub_dev_t *vhub, USB_NODE_INFORMATION &nodeinfo, ULONG inlen, ULONG &outlen)
{
	PAGED_CODE();

	outlen = sizeof(nodeinfo);

	if (inlen != sizeof(nodeinfo)) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	if (nodeinfo.NodeType == UsbMIParent) {
		nodeinfo.u.MiParentInformation.NumberOfInterfaces = 1;
	} else {
		vhub_get_hub_descriptor(vhub, nodeinfo.u.HubInformation.HubDescriptor);
		nodeinfo.u.HubInformation.HubIsBusPowered = false;
	}

	return STATUS_SUCCESS;
}

_IRQL_requires_(LOW_LEVEL)
PAGEABLE NTSTATUS get_nodeconn_info_ex_v2(vhub_dev_t *vhub, USB_NODE_CONNECTION_INFORMATION_EX_V2 &ci, ULONG inlen, ULONG &outlen)
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
	TraceMsg("ConnectionIndex %lu", ci.ConnectionIndex);

	ci.SupportedUsbProtocols.ul = 0; // by the port
	ci.SupportedUsbProtocols.Usb110 = true;
	ci.SupportedUsbProtocols.Usb200 = true;
	ci.SupportedUsbProtocols.Usb300 = true;

	ci.Flags.ul = 0;

	if (auto vpdo = vhub_find_vpdo(vhub, ci.ConnectionIndex)) {
		switch (vpdo->speed) {
		case USB_SPEED_SUPER_PLUS:
			ci.Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher = true;
			ci.Flags.DeviceIsSuperSpeedPlusCapableOrHigher = true;
			[[fallthrough]];
		case USB_SPEED_SUPER:
			ci.Flags.DeviceIsOperatingAtSuperSpeedOrHigher = true;
			ci.Flags.DeviceIsSuperSpeedCapableOrHigher = true;
		}
	}

	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_descr_from_nodeconn(vhub_dev_t *vhub, USB_DESCRIPTOR_REQUEST &r, ULONG inlen, ULONG &outlen)
{
	PAGED_CODE();

	auto &pkt = r.SetupPacket;

	UCHAR type = pkt.wValue >> 8;
	UCHAR idx  = pkt.wValue & 0xFF;

	USHORT lang_id = pkt.wIndex;

	TraceDbg("ConnectionIndex %lu, %!usb_descriptor_type!, index %d, lang_id %#x; inlen %lu, outlen %lu", 
		  r.ConnectionIndex, type, idx, lang_id, inlen, outlen);

	if (!(inlen >= sizeof(r) && outlen > sizeof(r))) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (auto vpdo = vhub_find_vpdo(vhub, r.ConnectionIndex)) {
		return do_get_descr_from_nodeconn(vpdo, r, outlen);
	}

	return STATUS_NO_SUCH_DEVICE;
}

_IRQL_requires_(LOW_LEVEL)
PAGEABLE NTSTATUS get_hub_information_ex(vhub_dev_t *vhub, USB_HUB_INFORMATION_EX &r, ULONG &outlen)
{
	PAGED_CODE();

	if (outlen == sizeof(r)) {
		return vhub_get_information_ex(vhub, r);
	}

	outlen = sizeof(r);
	return STATUS_INVALID_BUFFER_SIZE;
}

constexpr auto HubIsHighSpeedCapable()
{
	return false; // the hub is capable of running at high speed
}

_IRQL_requires_(LOW_LEVEL)
PAGEABLE NTSTATUS get_hub_capabilities(USB_HUB_CAPABILITIES &r, ULONG &outlen)
{
	PAGED_CODE();

	bool ok = outlen >= sizeof(r);
	outlen = sizeof(r);

	if (ok) {
		r.HubIs2xCapable = HubIsHighSpeedCapable();
	}

	return ok ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

_IRQL_requires_(LOW_LEVEL)
PAGEABLE NTSTATUS get_hub_capabilities_ex(USB_HUB_CAPABILITIES_EX &r, ULONG &outlen)
{
	PAGED_CODE();

	bool ok = outlen >= sizeof(r);
	outlen = sizeof(r);
	if (!ok) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &f = r.CapabilityFlags;
	f.ul = 0;

	f.HubIsHighSpeedCapable = HubIsHighSpeedCapable();
	f.HubIsHighSpeed = true;

	f.HubIsMultiTtCapable = true;
	f.HubIsMultiTt = true;

	f.HubIsRoot = true;
//	f.HubIsArmedWakeOnConnect = true; // the hub is armed to wake when a device is connected to the hub
//	f.HubIsBusPowered = false;

	return STATUS_SUCCESS;
}

_IRQL_requires_(LOW_LEVEL)
PAGEABLE NTSTATUS get_port_connector_properties(vhub_dev_t *vhub, USB_PORT_CONNECTOR_PROPERTIES &r, ULONG inlen, ULONG &outlen)
{
	PAGED_CODE();

	if (inlen >= sizeof(r)) {
		return vhub_get_port_connector_properties(vhub, r, outlen);
	}

	outlen = sizeof(r);
	return STATUS_BUFFER_TOO_SMALL;
}

_IRQL_requires_(LOW_LEVEL)
PAGEABLE NTSTATUS get_node_driverkey_name(vhub_dev_t *vhub, USB_NODE_CONNECTION_DRIVERKEY_NAME &r, ULONG inlen, ULONG &outlen)
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

_IRQL_requires_(LOW_LEVEL)
PAGEABLE NTSTATUS get_node_connection_attributes(vhub_dev_t *vhub, USB_NODE_CONNECTION_ATTRIBUTES &r, ULONG inlen, ULONG &outlen)
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


_IRQL_requires_(LOW_LEVEL)
PAGEABLE NTSTATUS vhci_ioctl_vhub(vhub_dev_t *vhub, ULONG ioctl_code, void *buffer, ULONG inlen, ULONG &outlen)
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
		status = get_hub_capabilities(*static_cast<USB_HUB_CAPABILITIES*>(buffer), outlen);
		break;
	case IOCTL_USB_GET_HUB_CAPABILITIES_EX:
		status = get_hub_capabilities_ex(*static_cast<USB_HUB_CAPABILITIES_EX*>(buffer), outlen);
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
		Trace(TRACE_LEVEL_ERROR, "Unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	return status;
}
