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

PAGEABLE NTSTATUS ioctl_vhub(vhub_dev_t &vhub, ULONG ioctl_code, void *buffer, ULONG inlen, ULONG &outlen)
{
	PAGED_CODE();

	auto st = STATUS_INVALID_DEVICE_REQUEST;

	switch (ioctl_code) {
	case IOCTL_USBIP_VHCI_PLUGIN_HARDWARE:
		st = inlen == sizeof(ioctl_usbip_vhci_plugin) && outlen == sizeof(ioctl_usbip_vhci_plugin::port) ? 
			plugin_vpdo(vhub, *static_cast<ioctl_usbip_vhci_plugin*>(buffer)) : STATUS_INVALID_BUFFER_SIZE;
		break;
	case IOCTL_USBIP_VHCI_UNPLUG_HARDWARE:
		outlen = 0;
		st = inlen == sizeof(ioctl_usbip_vhci_unplug) ? 
			unplug_vpdo(vhub, static_cast<ioctl_usbip_vhci_unplug*>(buffer)->port) : STATUS_INVALID_BUFFER_SIZE;
		break;
	case IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES:
		st = get_imported_devs(vhub, (ioctl_usbip_vhci_imported_dev*)buffer, outlen/sizeof(ioctl_usbip_vhci_imported_dev));
		break;
	case IOCTL_USB_GET_ROOT_HUB_NAME:
		st = get_roothub_name(vhub, *static_cast<USB_ROOT_HUB_NAME*>(buffer), outlen);
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "Unhandled %s(%#08lX)", device_control_name(ioctl_code), ioctl_code);
	}

	return st;
}

} // namespace


/*
 * On output USB_ROOT_HUB_NAME structure that contains the symbolic link name of the root hub. 
 * The leading "\xxx\ " text is not included in the retrieved string.
 */
PAGEABLE NTSTATUS get_roothub_name(_In_ vhub_dev_t &vhub, _Out_ USB_ROOT_HUB_NAME &r, _Out_ ULONG &outlen)
{
	PAGED_CODE();

	if (outlen < sizeof(r)) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto &src = vhub.DevIntfRootHub;

	auto prefix_cch = get_name_prefix_cch(src);
	if (!prefix_cch) {
		Trace(TRACE_LEVEL_WARNING, "Prefix expected: DevIntfRootHub '%!USTR!'", &src);
	}

	auto src_start = src.Buffer + prefix_cch;
	auto src_sz = src.Length - USHORT(prefix_cch*sizeof(*src.Buffer));

	r.ActualLength = sizeof(r) + src_sz; // NULL terminated, do not subtract sizeof(RootHubName)
	outlen = min(outlen, r.ActualLength);

	auto dest_start = r.RootHubName;
	USHORT dest_sz = USHORT(outlen) - offsetof(USB_ROOT_HUB_NAME, RootHubName);

	RtlStringCbCopyNW(dest_start, dest_sz, src_start, src_sz);
	
	UNICODE_STRING dest{ .Length = dest_sz, .MaximumLength = dest_sz, .Buffer = dest_start };
	TraceMsg("ActualLength %lu, RootHubName '%!USTR!'", r.ActualLength, &dest);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS get_hcd_driverkey_name(vhci_dev_t &vhci, USB_HCD_DRIVERKEY_NAME &r, ULONG &outlen)
{
	PAGED_CODE();

	if (outlen < sizeof(r)) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto err = STATUS_SUCCESS;
	ULONG prop_sz = 0;

	auto prop = (PWSTR)GetDeviceProperty(vhci.child_pdo->Self, DevicePropertyDriverKeyName, err, prop_sz); // NULL terminated
	if (!prop) {
		return err;
	}

	ULONG r_sz = sizeof(r) - sizeof(*r.DriverKeyName) + prop_sz;
	outlen = min(outlen, r_sz);

	auto dest_start = r.DriverKeyName;
	USHORT dest_sz = USHORT(outlen) - offsetof(USB_HCD_DRIVERKEY_NAME, DriverKeyName);

	r.ActualLength = prop_sz;
	RtlStringCbCopyNW(dest_start, dest_sz, prop, prop_sz);

	UNICODE_STRING dest{ .Length = dest_sz, .MaximumLength = dest_sz, .Buffer = dest_start };
	TraceMsg("ActualLength %lu, DriverKeyName '%!USTR!'", r.ActualLength, &dest);

	ExFreePoolWithTag(prop, USBIP_VHCI_POOL_TAG);
	return err;
}

PAGEABLE NTSTATUS vhci_ioctl_vhci(vhci_dev_t &vhci, ULONG ioctl_code, void *buffer, ULONG inlen, ULONG &outlen)
{
	PAGED_CODE();

	auto st = STATUS_NO_SUCH_DEVICE;

	switch (ioctl_code) {
	case IOCTL_GET_HCD_DRIVERKEY_NAME:
		st = get_hcd_driverkey_name(vhci, *static_cast<USB_HCD_DRIVERKEY_NAME*>(buffer), outlen);
		break;
	case IOCTL_USB_USER_REQUEST:
		NT_ASSERT(inlen == outlen);
		st = vhci_ioctl_user_request(vhci, static_cast<USBUSER_REQUEST_HEADER*>(buffer), outlen);
		break;
	default:
		if (auto vhub = vhub_from_vhci(&vhci)) {
			st = ioctl_vhub(*vhub, ioctl_code, buffer, inlen, outlen);
		} else {
			TraceMsg("vhub has gone");
		}
	}

	return st;
}
