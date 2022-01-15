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

PAGEABLE NTSTATUS get_hcd_driverkey_name(vhci_dev_t *vhci, USB_HCD_DRIVERKEY_NAME &r, ULONG *poutlen)
{
	PAGED_CODE();

	ULONG prop_sz = 0;
	auto prop = get_device_prop(vhci->child_pdo->Self, DevicePropertyDriverKeyName, &prop_sz);
	if (!prop) {
		Trace(TRACE_LEVEL_ERROR, "Failed to get DevicePropertyDriverKeyName");
		return STATUS_UNSUCCESSFUL;
	}

	ULONG r_sz = sizeof(r) - sizeof(*r.DriverKeyName) + prop_sz;

	if (*poutlen < sizeof(r)) {
		*poutlen = r_sz;
		ExFreePoolWithTag(prop, USBIP_VHCI_POOL_TAG);
		return STATUS_BUFFER_TOO_SMALL;
	}

	*poutlen = min(*poutlen, r_sz);

	r.ActualLength = prop_sz;
	RtlCopyMemory(r.DriverKeyName, prop, *poutlen - offsetof(USB_HCD_DRIVERKEY_NAME, DriverKeyName));

	ExFreePoolWithTag(prop, USBIP_VHCI_POOL_TAG);
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

PAGEABLE NTSTATUS vhci_ioctl_vhci(vhci_dev_t *vhci, IO_STACK_LOCATION *irpstack, ULONG ioctl_code, void  *buffer, ULONG inlen, ULONG *poutlen)
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
		status = get_hcd_driverkey_name(vhci, *static_cast<USB_HCD_DRIVERKEY_NAME*>(buffer), poutlen);
		break;
	case IOCTL_USB_GET_ROOT_HUB_NAME:
		status = vhub_get_roothub_name(to_vhub_or_null(vhci->child_pdo->fdo->Self), buffer, inlen, poutlen);
		break;
	case IOCTL_USB_USER_REQUEST:
		status = vhci_ioctl_user_request(vhci, buffer, inlen, poutlen);
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	return status;
}
