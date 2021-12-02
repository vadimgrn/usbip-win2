#include "vhci_ioctl_vhci.h"
#include "dbgcommon.h"
#include "trace.h"
#include "vhci_ioctl_vhci.tmh"

#include "vhci.h"
#include "vhci_plugin.h"
#include "vhci_pnp.h"
#include "vhci_vhub.h"
#include "vhci_ioctl_usrreq.h"

#include <usbdi.h>
#include <usbuser.h>

static PAGEABLE NTSTATUS
get_hcd_driverkey_name(pvhci_dev_t vhci, PVOID buffer, PULONG poutlen)
{
	PUSB_HCD_DRIVERKEY_NAME	pdrkey_name = (PUSB_HCD_DRIVERKEY_NAME)buffer;
	ULONG	outlen_res, drvkey_buflen;
	LPWSTR	drvkey;

	drvkey = get_device_prop(vhci->common.child_pdo->Self, DevicePropertyDriverKeyName, &drvkey_buflen);
	if (drvkey == NULL) {
		TraceWarning(TRACE_IOCTL, "failed to get vhci driver key");
		return STATUS_UNSUCCESSFUL;
	}

	outlen_res = (ULONG)(sizeof(USB_HCD_DRIVERKEY_NAME) + drvkey_buflen - sizeof(WCHAR));
	if (*poutlen < sizeof(USB_HCD_DRIVERKEY_NAME)) {
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
static PAGEABLE SIZE_T
get_name_prefix_size(PWCHAR name)
{
	SIZE_T	i;
	for (i = 1; name[i]; i++) {
		if (name[i] == L'\\') {
			return i + 1;
		}
	}
	return 0;
}

PAGEABLE NTSTATUS
vhub_get_roothub_name(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_ROOT_HUB_NAME	roothub_name = (PUSB_ROOT_HUB_NAME)buffer;
	SIZE_T	roothub_namelen, prefix_len;

	UNREFERENCED_PARAMETER(inlen);

	prefix_len = get_name_prefix_size(vhub->DevIntfRootHub.Buffer);
	if (prefix_len == 0) {
		TraceError(TRACE_HPDO, "inavlid root hub format: %S", vhub->DevIntfRootHub.Buffer);
		return STATUS_INVALID_PARAMETER;
	}
	roothub_namelen = sizeof(USB_ROOT_HUB_NAME) + vhub->DevIntfRootHub.Length - prefix_len * sizeof(WCHAR);
	if (*poutlen < sizeof(USB_ROOT_HUB_NAME)) {
		*poutlen = (ULONG)roothub_namelen;
		return STATUS_BUFFER_TOO_SMALL;
	}
	roothub_name->ActualLength = (ULONG)roothub_namelen;
	RtlStringCchCopyW(roothub_name->RootHubName, (*poutlen - sizeof(USB_ROOT_HUB_NAME) + sizeof(WCHAR)) / sizeof(WCHAR),
		vhub->DevIntfRootHub.Buffer + prefix_len);
	if (*poutlen > roothub_namelen)
		*poutlen = (ULONG)roothub_namelen;
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhci_ioctl_vhci(pvhci_dev_t vhci, PIO_STACK_LOCATION irpstack, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG *poutlen)
{
	NTSTATUS	status = STATUS_INVALID_DEVICE_REQUEST;

	switch (ioctl_code) {
	case IOCTL_USBIP_VHCI_PLUGIN_HARDWARE:
		status = vhci_plugin_vpdo(vhci, (pvhci_pluginfo_t)buffer, inlen, irpstack->FileObject);
		*poutlen = sizeof(vhci_pluginfo_t);
		break;
	case IOCTL_USBIP_VHCI_GET_PORTS_STATUS:
		if (*poutlen == sizeof(ioctl_usbip_vhci_get_ports_status))
			status = vhub_get_ports_status(vhub_from_vhci(vhci), (ioctl_usbip_vhci_get_ports_status *)buffer);
		break;
	case IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES:
		status = vhub_get_imported_devs(vhub_from_vhci(vhci), (pioctl_usbip_vhci_imported_dev_t)buffer, poutlen);
		break;
	case IOCTL_USBIP_VHCI_UNPLUG_HARDWARE:
		if (inlen == sizeof(ioctl_usbip_vhci_unplug))
			status = vhci_unplug_port(vhci, ((ioctl_usbip_vhci_unplug *)buffer)->addr);
		*poutlen = 0;
		break;
	case IOCTL_GET_HCD_DRIVERKEY_NAME:
		status = get_hcd_driverkey_name(vhci, buffer, poutlen);
		break;
	case IOCTL_USB_GET_ROOT_HUB_NAME:
		status = vhub_get_roothub_name(devobj_to_vhub_or_null(vhci->common.child_pdo->fdo->Self), buffer, inlen, poutlen);
		break;
	case IOCTL_USB_USER_REQUEST:
		status = vhci_ioctl_user_request(vhci, buffer, inlen, poutlen);
		break;
	default:
		TraceError(TRACE_IOCTL, "unhandled %s(%#010lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
		break;
	}

	return status;
}
