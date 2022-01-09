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

	if (!(inlen >= sizeof(ci) && *poutlen >= sizeof(ci))) {
		*poutlen = sizeof(ci);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (auto vpdo = vhub_find_vpdo(vhub, ci.ConnectionIndex)) {
		auto st = vpdo_get_nodeconn_info(vpdo, ci, poutlen, ex);
		vdev_del_ref(vpdo);
		return st;
	}

	return STATUS_NO_SUCH_DEVICE;
}

PAGEABLE NTSTATUS get_nodeconn_info_ex_v2(vhub_dev_t *vhub, void *buffer, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto conninfo = (USB_NODE_CONNECTION_INFORMATION_EX_V2*)buffer;

	if (inlen < sizeof(*conninfo) || *poutlen < sizeof(*conninfo)) {
		*poutlen = sizeof(*conninfo);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (auto vpdo = vhub_find_vpdo(vhub, conninfo->ConnectionIndex)) {
		auto st = vpdo_get_nodeconn_info_ex_v2(vpdo, *conninfo, poutlen);
		vdev_del_ref(vpdo);
		return st;
	}

	return STATUS_NO_SUCH_DEVICE;
}

PAGEABLE NTSTATUS get_descriptor_from_nodeconn(vhub_dev_t *vhub, IRP *irp, void *buffer, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto r = static_cast<USB_DESCRIPTOR_REQUEST*>(buffer);

	if (inlen < sizeof(*r)) {
		*poutlen = sizeof(*r);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (auto vpdo = vhub_find_vpdo(vhub, r->ConnectionIndex)) {
		auto st = vpdo_get_dsc_from_nodeconn(vpdo, irp, r, poutlen);
		vdev_del_ref(vpdo);
		return st;
	}

	return STATUS_NO_SUCH_DEVICE;
}

PAGEABLE NTSTATUS get_hub_information_ex(vhub_dev_t *vhub, void *buffer, ULONG *poutlen)
{
	PAGED_CODE();

	auto r = (USB_HUB_INFORMATION_EX*)buffer;
	if (*poutlen == sizeof(*r)) {
		return vhub_get_information_ex(vhub, *r);
	}

	*poutlen = sizeof(*r);
	return STATUS_INVALID_BUFFER_SIZE;
}

PAGEABLE NTSTATUS get_hub_capabilities_ex(vhub_dev_t * vhub, void *buffer, ULONG *poutlen)
{
	PAGED_CODE();

	auto r = (USB_HUB_CAPABILITIES_EX*)buffer;
	if (*poutlen == sizeof(*r)) {
		return vhub_get_capabilities_ex(vhub, *r);
	}

	*poutlen = sizeof(*r);
	return STATUS_INVALID_BUFFER_SIZE;
}

PAGEABLE NTSTATUS get_port_connector_properties(vhub_dev_t *vhub, void *buffer, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto r = (USB_PORT_CONNECTOR_PROPERTIES*)buffer;
	if (inlen >= sizeof(*r)) {
		return vhub_get_port_connector_properties(vhub, r, poutlen);
	}

	*poutlen = sizeof(*r);
	return STATUS_BUFFER_TOO_SMALL;
}

PAGEABLE NTSTATUS get_node_driverkey_name(vhub_dev_t *vhub, void *buffer, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	auto pdrvkey_name = (USB_NODE_CONNECTION_DRIVERKEY_NAME*)buffer;

	if (inlen < sizeof(*pdrvkey_name)) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto vpdo = vhub_find_vpdo(vhub, pdrvkey_name->ConnectionIndex);
	if (!vpdo) {
		return STATUS_NO_SUCH_DEVICE;
	}

	NTSTATUS status = STATUS_SUCCESS;

	ULONG driverkeylen = 0;
	auto driverkey = get_device_prop(vpdo->Self, DevicePropertyDriverKeyName, &driverkeylen);

	if (!driverkey) {
		Trace(TRACE_LEVEL_WARNING, "failed to get vpdo driver key");
		status = STATUS_UNSUCCESSFUL;
	} else {
		ULONG outlen_res = sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME) + driverkeylen - sizeof(WCHAR);

		if (*poutlen < sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME)) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			*poutlen = outlen_res;
		} else {
			pdrvkey_name->ActualLength = outlen_res;
			if (*poutlen >= outlen_res) {
				RtlCopyMemory(pdrvkey_name->DriverKeyName, driverkey, driverkeylen);
				*poutlen = outlen_res;
			} else {
				RtlCopyMemory(pdrvkey_name->DriverKeyName, driverkey, *poutlen - sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME) + sizeof(WCHAR));
			}

			status = STATUS_SUCCESS;
		}

		ExFreePoolWithTag(driverkey, USBIP_VHCI_POOL_TAG);
	}

	vdev_del_ref(vpdo);
	return status;
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
		status = get_nodeconn_info_ex_v2(vhub, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		status = get_descriptor_from_nodeconn(vhub, irp, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_HUB_INFORMATION_EX:
		status = get_hub_information_ex(vhub, buffer, poutlen);
		break;
	case IOCTL_USB_GET_HUB_CAPABILITIES_EX:
		status = get_hub_capabilities_ex(vhub, buffer, poutlen);
		break;
	case IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES:
		status = get_port_connector_properties(vhub, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME:
		status = get_node_driverkey_name(vhub, buffer, inlen, poutlen);
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "Unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	return status;
}
