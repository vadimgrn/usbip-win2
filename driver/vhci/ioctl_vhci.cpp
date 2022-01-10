#include "ioctl_vhci.h"
#include "dbgcommon.h"
#include "trace.h"
#include "ioctl_vhci.tmh"

#include "vhci.h"
#include "plugin.h"
#include "pnp.h"
#include "vhub.h"
#include "ioctl_usrreq.h"

#include <usbdi.h>
#include <usbuser.h>
#include <ntstrsafe.h>

namespace
{

PAGEABLE NTSTATUS get_hcd_driverkey_name(vhci_dev_t *vhci, void *buffer, ULONG *poutlen)
{
	PAGED_CODE();

	auto pdrkey_name = (USB_HCD_DRIVERKEY_NAME*)buffer;
	ULONG drvkey_buflen = 0;

	auto drvkey = get_device_prop(vhci->child_pdo->Self, DevicePropertyDriverKeyName, &drvkey_buflen);
	if (!drvkey) {
		Trace(TRACE_LEVEL_WARNING, "failed to get vhci driver key");
		return STATUS_UNSUCCESSFUL;
	}

	auto outlen_res = (ULONG)(sizeof(*pdrkey_name) + drvkey_buflen - sizeof(WCHAR));
	if (*poutlen < sizeof(*pdrkey_name)) {
		*poutlen = outlen_res;
		ExFreePoolWithTag(drvkey, USBIP_VHCI_POOL_TAG);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	pdrkey_name->ActualLength = outlen_res;
	if (*poutlen >= outlen_res) {
		RtlCopyMemory(pdrkey_name->DriverKeyName, drvkey, drvkey_buflen);
		*poutlen = outlen_res;
	}
	else
		RtlCopyMemory(pdrkey_name->DriverKeyName, drvkey, *poutlen - sizeof(USB_HCD_DRIVERKEY_NAME) + sizeof(WCHAR));

	ExFreePoolWithTag(drvkey, USBIP_VHCI_POOL_TAG);

	return STATUS_SUCCESS;
}

/* IOCTL_USB_GET_ROOT_HUB_NAME requires a device interface symlink name with the prefix(\??\) stripped */
PAGEABLE SIZE_T get_name_prefix_size(PWCHAR name)
{
	PAGED_CODE();

	SIZE_T	i;
	for (i = 1; name[i]; i++) {
		if (name[i] == L'\\') {
			return i + 1;
		}
	}
	return 0;
}

} // namespace


PAGEABLE NTSTATUS vhub_get_roothub_name(vhub_dev_t * vhub, PVOID buffer, ULONG, PULONG poutlen)
{
	PAGED_CODE();

	auto roothub_name = (USB_ROOT_HUB_NAME*)buffer;
	auto prefix_len = get_name_prefix_size(vhub->DevIntfRootHub.Buffer);

	if (!prefix_len) {
		Trace(TRACE_LEVEL_ERROR, "inavlid root hub format: %S", vhub->DevIntfRootHub.Buffer);
		return STATUS_INVALID_PARAMETER;
	}

	auto roothub_namelen = sizeof(USB_ROOT_HUB_NAME) + vhub->DevIntfRootHub.Length - prefix_len * sizeof(WCHAR);

	if (*poutlen < sizeof(USB_ROOT_HUB_NAME)) {
		*poutlen = (ULONG)roothub_namelen;
		return STATUS_BUFFER_TOO_SMALL;
	}

	roothub_name->ActualLength = (ULONG)roothub_namelen;
	RtlStringCchCopyW(roothub_name->RootHubName, (*poutlen - sizeof(USB_ROOT_HUB_NAME) + sizeof(WCHAR)) / sizeof(WCHAR),
			vhub->DevIntfRootHub.Buffer + prefix_len);

	if (*poutlen > roothub_namelen) {
		*poutlen = (ULONG)roothub_namelen;
	}

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS vhci_ioctl_vhci(vhci_dev_t * vhci, PIO_STACK_LOCATION irpstack, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG *poutlen)
{
	PAGED_CODE();

	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

	switch (ioctl_code) {
	case IOCTL_USBIP_VHCI_PLUGIN_HARDWARE:
		status = vhci_plugin_vpdo(vhci, static_cast<vhci_pluginfo_t*>(buffer), inlen, irpstack->FileObject);
		*poutlen = sizeof(vhci_pluginfo_t);
		break;
	case IOCTL_USBIP_VHCI_UNPLUG_HARDWARE:
		*poutlen = 0;
		status = inlen == sizeof(ioctl_usbip_vhci_unplug) ? 
			vhci_unplug_vpdo(vhci, static_cast<ioctl_usbip_vhci_unplug*>(buffer)->addr) :
			STATUS_INVALID_BUFFER_SIZE;
		break;
	case IOCTL_USBIP_VHCI_GET_PORTS_STATUS:
		status = vhub_get_ports_status(vhub_from_vhci(vhci), *static_cast<ioctl_usbip_vhci_get_ports_status*>(buffer), poutlen);
		break;
	case IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES:
		status = vhub_get_imported_devs(vhub_from_vhci(vhci), (ioctl_usbip_vhci_imported_dev*)buffer, 
						*poutlen/sizeof(ioctl_usbip_vhci_imported_dev));
		break;
	case IOCTL_GET_HCD_DRIVERKEY_NAME:
		status = get_hcd_driverkey_name(vhci, buffer, poutlen);
		break;
	case IOCTL_USB_GET_ROOT_HUB_NAME:
		status = vhub_get_roothub_name(devobj_to_vhub_or_null(vhci->child_pdo->fdo->Self), buffer, inlen, poutlen);
		break;
	case IOCTL_USB_USER_REQUEST:
		status = vhci_ioctl_user_request(vhci, buffer, inlen, poutlen);
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	return status;
}
