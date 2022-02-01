#include "ioctl_vhub.h"
#include "dbgcommon.h"
#include "trace.h"
#include "ioctl_vhub.tmh"

#include "ioctl_vhci.h"
#include "vhci.h"
#include "pnp.h"
#include "vpdo.h"
#include "vpdo_dsc.h"
#include "vhub.h"

namespace
{

PAGEABLE NTSTATUS get_node_info(vhub_dev_t *vhub, USB_NODE_INFORMATION &nodeinfo, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	*poutlen = sizeof(nodeinfo);

	if (inlen != sizeof(nodeinfo)) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	if (nodeinfo.NodeType == UsbMIParent) {
		nodeinfo.u.MiParentInformation.NumberOfInterfaces = 1;
	} else {
		vhub_get_hub_descriptor(vhub, nodeinfo.u.HubInformation.HubDescriptor);
		nodeinfo.u.HubInformation.HubIsBusPowered = FALSE;
	}

	return STATUS_SUCCESS;
}

PAGEABLE auto get_nodeconn_info(vhub_dev_t *vhub, void *buffer, ULONG inlen, ULONG *poutlen, bool ex)
{
	PAGED_CODE();

	static_assert(sizeof(USB_NODE_CONNECTION_INFORMATION) == sizeof(USB_NODE_CONNECTION_INFORMATION_EX));
	auto &ci = *reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(buffer);

	if (inlen < sizeof(ci) || *poutlen < sizeof(ci)) {
		*poutlen = sizeof(ci);
		return STATUS_BUFFER_TOO_SMALL;
	}

	NT_ASSERT(ci.ConnectionIndex);
	auto vpdo = vhub_find_vpdo(vhub, ci.ConnectionIndex);

	return vpdo_get_nodeconn_info(vpdo, ci, poutlen, ex);
}

PAGEABLE NTSTATUS get_nodeconn_info_ex_v2(vhub_dev_t *vhub, USB_NODE_CONNECTION_INFORMATION_EX_V2 &ci, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();
	
	if (!(inlen == sizeof(ci) && *poutlen == sizeof(ci))) {
		*poutlen = sizeof(ci);
		return STATUS_INVALID_BUFFER_SIZE;
	}

	if (ci.Length != sizeof(ci)) {
		return STATUS_INVALID_PARAMETER;
	}

	NT_ASSERT(ci.ConnectionIndex);
	TraceCall("ConnectionIndex %lu", ci.ConnectionIndex);

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

PAGEABLE NTSTATUS get_descriptor_from_nodeconn(vhub_dev_t *vhub, IRP *irp, USB_DESCRIPTOR_REQUEST &r, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	if (inlen < sizeof(r)) {
		*poutlen = sizeof(r);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (auto vpdo = vhub_find_vpdo(vhub, r.ConnectionIndex)) {
		auto st = vpdo_get_dsc_from_nodeconn(vpdo, irp, r, poutlen);
		return st;
	}

	return STATUS_NO_SUCH_DEVICE;
}

PAGEABLE NTSTATUS get_hub_information_ex(vhub_dev_t *vhub, USB_HUB_INFORMATION_EX &r, ULONG *poutlen)
{
	PAGED_CODE();

	if (*poutlen == sizeof(r)) {
		return vhub_get_information_ex(vhub, r);
	}

	*poutlen = sizeof(r);
	return STATUS_INVALID_BUFFER_SIZE;
}

constexpr auto HubIsHighSpeedCapable()
{
	return false; // the hub is capable of running at high speed
}

PAGEABLE NTSTATUS get_hub_capabilities(USB_HUB_CAPABILITIES &r, ULONG *poutlen)
{
	PAGED_CODE();

	bool ok = *poutlen >= sizeof(r);
	*poutlen = sizeof(r);

	if (ok) {
		r.HubIs2xCapable = HubIsHighSpeedCapable();
	}

	return ok ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

PAGEABLE NTSTATUS get_hub_capabilities_ex(USB_HUB_CAPABILITIES_EX &r, ULONG *poutlen)
{
	PAGED_CODE();

	bool ok = *poutlen >= sizeof(r);
	*poutlen = sizeof(r);
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

PAGEABLE NTSTATUS get_port_connector_properties(vhub_dev_t *vhub, USB_PORT_CONNECTOR_PROPERTIES &r, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	if (inlen >= sizeof(r)) {
		return vhub_get_port_connector_properties(vhub, r, poutlen);
	}

	*poutlen = sizeof(r);
	return STATUS_BUFFER_TOO_SMALL;
}

PAGEABLE NTSTATUS get_node_driverkey_name(vhub_dev_t *vhub, USB_NODE_CONNECTION_DRIVERKEY_NAME &r, ULONG inlen, ULONG *poutlen)
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

	auto driverkey = GetDevicePropertyString(vpdo->Self, DevicePropertyDriverKeyName, status, driverkeylen);
	if (!driverkey) {
		return status;
	}

	ULONG outlen_res = sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME) + driverkeylen - sizeof(WCHAR);

	if (*poutlen >= sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME)) {
		r.ActualLength = outlen_res;
		if (*poutlen >= outlen_res) {
			RtlCopyMemory(r.DriverKeyName, driverkey, driverkeylen);
			*poutlen = outlen_res;
		} else {
			RtlCopyMemory(r.DriverKeyName, driverkey, *poutlen - sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME) + sizeof(WCHAR));
		}
	} else {
		status = STATUS_INSUFFICIENT_RESOURCES;
		*poutlen = outlen_res;
	}

	ExFreePool(driverkey);
	return status;
}

PAGEABLE NTSTATUS get_node_connection_attributes(vhub_dev_t *vhub, USB_NODE_CONNECTION_ATTRIBUTES &r, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	if (!(inlen == sizeof(r) && *poutlen == sizeof(r))) {
		*poutlen = sizeof(r);
		return STATUS_INVALID_BUFFER_SIZE;
	}

	NT_ASSERT(r.ConnectionIndex);
	TraceCall("ConnectionIndex %lu", r.ConnectionIndex);

	auto vpdo = vhub_find_vpdo(vhub, r.ConnectionIndex);

	r.ConnectionStatus = vpdo ? DeviceConnected : NoDeviceConnected;
	r.PortAttributes = USB_PORTATTR_NO_OVERCURRENT_UI;
	
	return STATUS_SUCCESS;
}

} // namespace


PAGEABLE NTSTATUS vhci_ioctl_vhub(vhub_dev_t *vhub, IRP *irp, ULONG ioctl_code, void *buffer, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

	switch (ioctl_code) {
	case IOCTL_USB_GET_NODE_INFORMATION:
		status = get_node_info(vhub, *static_cast<USB_NODE_INFORMATION*>(buffer), inlen, poutlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION:
		status = get_nodeconn_info(vhub, buffer, inlen, poutlen, false);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX:
		status = get_nodeconn_info(vhub, buffer, inlen, poutlen, true);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2:
		status = get_nodeconn_info_ex_v2(vhub, *reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX_V2*>(buffer), inlen, poutlen);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		status = get_descriptor_from_nodeconn(vhub, irp, *static_cast<USB_DESCRIPTOR_REQUEST*>(buffer), inlen, poutlen);
		break;
	case IOCTL_USB_GET_HUB_INFORMATION_EX:
		status = get_hub_information_ex(vhub, *static_cast<USB_HUB_INFORMATION_EX*>(buffer), poutlen);
		break;
	case IOCTL_USB_GET_HUB_CAPABILITIES:
		status = get_hub_capabilities(*static_cast<USB_HUB_CAPABILITIES*>(buffer), poutlen);
		break;
	case IOCTL_USB_GET_HUB_CAPABILITIES_EX:
		status = get_hub_capabilities_ex(*static_cast<USB_HUB_CAPABILITIES_EX*>(buffer), poutlen);
		break;
	case IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES:
		status = get_port_connector_properties(vhub, *static_cast<USB_PORT_CONNECTOR_PROPERTIES*>(buffer), inlen, poutlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME:
		status = get_node_driverkey_name(vhub, *static_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(buffer), inlen, poutlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_ATTRIBUTES:
		status = get_node_connection_attributes(vhub, *static_cast<USB_NODE_CONNECTION_ATTRIBUTES*>(buffer), inlen, poutlen);
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "Unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	return status;
}
