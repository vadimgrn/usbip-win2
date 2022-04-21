#include "ioctl_vhci.h"
#include "trace.h"
#include "ioctl_vhci.tmh"

#include "dbgcommon.h"
#include "vhci.h"
#include "plugin.h"
#include "vhub.h"
#include "ioctl_usrreq.h"

#include <usbuser.h>
#include <ntstrsafe.h>

namespace
{

/* 
 * The leading "\xxx\ " text is not included in the retrieved string.
 */
PAGEABLE ULONG get_name_prefix_cch(const UNICODE_STRING &s)
{
	PAGED_CODE();

	auto &str = s.Buffer;

	for (ULONG i = 1; *str == L'\\' && (i + 1)*sizeof(*str) <= s.Length; ++i) {
		if (str[i] == L'\\') {
			return i + 1;
		}
	}

	return 0;
}

} // namespace


PAGEABLE NTSTATUS vhub_get_roothub_name(vhub_dev_t *vhub, USB_ROOT_HUB_NAME &r, ULONG &outlen)
{
	PAGED_CODE();

	auto &str = vhub->DevIntfRootHub;

	auto prefix_cch = get_name_prefix_cch(str);
	if (!prefix_cch) {
		Trace(TRACE_LEVEL_WARNING, "Prefix expected: DevIntfRootHub '%!USTR!'", &str);
	}

	ULONG str_sz = str.Length - prefix_cch*sizeof(*str.Buffer);
	ULONG r_sz = sizeof(r) - sizeof(*r.RootHubName) + str_sz;

	if (outlen < sizeof(r)) {
		outlen = r_sz;
		return STATUS_BUFFER_TOO_SMALL;
	}

	outlen = min(outlen, r_sz);

	r.ActualLength = r_sz;
	RtlStringCbCopyW(r.RootHubName, outlen - offsetof(USB_ROOT_HUB_NAME, RootHubName), str.Buffer + prefix_cch);
	
	TraceCall("ActualLength %lu, RootHubName '%S'", r.ActualLength, r.RootHubName);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS get_hcd_driverkey_name(vhci_dev_t *vhci, USB_HCD_DRIVERKEY_NAME &r, ULONG &outlen)
{
	PAGED_CODE();

	NTSTATUS st{};
	ULONG prop_sz = 0;

	auto prop = (PWSTR)GetDeviceProperty(vhci->child_pdo->Self, DevicePropertyDriverKeyName, st, prop_sz);
	if (!prop) {
		return st;
	}

	ULONG r_sz = sizeof(r) - sizeof(*r.DriverKeyName) + prop_sz;

	if (outlen >= sizeof(r)) {
		outlen = min(outlen, r_sz);
		r.ActualLength = prop_sz;
		RtlStringCbCopyW(r.DriverKeyName, outlen - offsetof(USB_HCD_DRIVERKEY_NAME, DriverKeyName), prop);
		TraceCall("ActualLength %lu, DriverKeyName '%S'", r.ActualLength, r.DriverKeyName);
	} else {
		outlen = r_sz;
		st = STATUS_BUFFER_TOO_SMALL;
	}

	ExFreePoolWithTag(prop, USBIP_VHCI_POOL_TAG);
	return st;
}

PAGEABLE NTSTATUS vhci_ioctl_vhci(IRP *irp, vhci_dev_t *vhci, ULONG ioctl_code, void *buffer, ULONG inlen, ULONG &outlen)
{
	PAGED_CODE();

	auto st = STATUS_INVALID_DEVICE_REQUEST;

	switch (ioctl_code) {
	case IOCTL_USBIP_VHCI_PLUGIN_HARDWARE:
		st = inlen == sizeof(ioctl_usbip_vhci_plugin) && outlen == sizeof(ioctl_usbip_vhci_plugin::port) ? 
                        vhci_plugin_vpdo(irp, vhci, *static_cast<ioctl_usbip_vhci_plugin*>(buffer)) : 
                        STATUS_INVALID_BUFFER_SIZE;
                break;
	case IOCTL_USBIP_VHCI_UNPLUG_HARDWARE:
		outlen = 0;
		st = inlen == sizeof(ioctl_usbip_vhci_unplug) ? 
			vhci_unplug_vpdo(vhci, static_cast<ioctl_usbip_vhci_unplug*>(buffer)->port) :
			STATUS_INVALID_BUFFER_SIZE;
		break;
	case IOCTL_USBIP_VHCI_GET_PORTS_STATUS:
		st = vhub_get_ports_status(vhub_from_vhci(vhci), *static_cast<ioctl_usbip_vhci_get_ports_status*>(buffer), outlen);
		break;
	case IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES:
		st = vhub_get_imported_devs(vhub_from_vhci(vhci), (ioctl_usbip_vhci_imported_dev*)buffer, 
						outlen/sizeof(ioctl_usbip_vhci_imported_dev));
		break;
	case IOCTL_GET_HCD_DRIVERKEY_NAME:
		st = get_hcd_driverkey_name(vhci, *static_cast<USB_HCD_DRIVERKEY_NAME*>(buffer), outlen);
		break;
	case IOCTL_USB_GET_ROOT_HUB_NAME:
		st = vhub_get_roothub_name(vhub_from_vhci(vhci), *static_cast<USB_ROOT_HUB_NAME*>(buffer), outlen);
		break;
	case IOCTL_USB_USER_REQUEST:
		st = vhci_ioctl_user_request(vhci, static_cast<USBUSER_REQUEST_HEADER*>(buffer), inlen, outlen);
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "Unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	return st;
}
