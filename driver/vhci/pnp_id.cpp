#include "pnp_id.h"
#include "trace.h"
#include "pnp_id.tmh"

#include "vhci.h"
#include "pnp.h"
#include "usbip_vhci_api.h"
#include "irp.h"

#include <ntstrsafe.h>

namespace
{

#define DEVID_VHCI	HWID_VHCI
#define DEVID_VHUB	HWID_VHUB

/*
* The first hardware ID in the list should be the device ID, and
* the remaining IDs should be listed in order of decreasing suitability.
*/
#define HWIDS_VHCI	DEVID_VHCI L"\0"

#define HWIDS_VHUB \
	DEVID_VHUB L"\0" \
	VHUB_PREFIX L"&VID_" VHUB_VID L"&PID_" VHUB_PID L"\0"

// vdev_type_t is an index
const LPCWSTR vdev_devids[] = {
	nullptr, DEVID_VHCI,
	nullptr, DEVID_VHUB,
	nullptr, L"USB\\VID_%04hx&PID_%04hx" // 21 chars after formatting
};

const size_t vdev_devid_size[] = {
	0, sizeof(DEVID_VHCI),
	0, sizeof(DEVID_VHUB),
	0, (21 + 1)*sizeof(WCHAR)
};

const LPCWSTR vdev_hwids[] = {
	nullptr, HWIDS_VHCI,
	nullptr, HWIDS_VHUB,
	nullptr, L"USB\\VID_%04hx&PID_%04hx&REV_%04hx;" // 31 chars after formatting
	L"USB\\VID_%04hx&PID_%04hx;" // 22 chars after formatting
};

const size_t vdev_hwids_size[] = {
	0, sizeof(HWIDS_VHCI),
	0, sizeof(HWIDS_VHUB),
	0, (31 + 22 + 1)*sizeof(WCHAR)
};

void subst_char(wchar_t *s, wchar_t ch, wchar_t rep)
{
	for ( ; *s; ++s) {
		if (*s == ch) {
			*s = rep;
		}
	}
}

/*
Enumeration of USB Composite Devices.

The bus driver also reports a compatible identifier (ID) of USB\COMPOSITE,
if the device meets the following requirements:
* The device class field of the device descriptor (bDeviceClass) must contain a value of zero,
or the class (bDeviceClass), subclass (bDeviceSubClass), and protocol (bDeviceProtocol) fields
of the device descriptor must have the values 0xEF, 0x02 and 0x01 respectively, as explained
in USB Interface Association Descriptor.
* The device must have multiple interfaces.
* The device must have a single configuration.

The bus driver also checks the device class (bDeviceClass), subclass (bDeviceSubClass),
and protocol (bDeviceProtocol) fields of the device descriptor. If these fields are zero,
the device is a composite device, and the bus driver reports an extra compatible
identifier (ID) of USB\COMPOSITE for the PDO.
*/
bool is_composite(vpdo_dev_t *vpdo)
{
	bool ok = !vpdo->usbclass || // generic composite device
		(vpdo->usbclass == 0xEF &&
			vpdo->subclass == 0x02 &&
			vpdo->protocol == 0x01); // IAD composite device

	return ok && vpdo->NumInterfaces > 1 && vpdo->NumConfigurations == 1;
}

/*
* For all USB devices, the USB bus driver reports a device ID with the following format:
* USB\VID_xxxx&PID_yyyy
*/
NTSTATUS setup_device_id(PWCHAR *result, bool*, vdev_t *vdev, IRP*)
{
	NTSTATUS status = STATUS_SUCCESS;

	auto str = vdev_devids[vdev->type];
	if (!str) {
		return STATUS_NOT_SUPPORTED;
	}

	auto str_sz = vdev_devid_size[vdev->type];

	auto id_dev = (PWCHAR)ExAllocatePoolWithTag(PagedPool, str_sz, USBIP_VHCI_POOL_TAG);
	if (!id_dev) {
		Trace(TRACE_LEVEL_ERROR, "%!vdev_type_t!: query device id: out of memory", vdev->type);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (vdev->type == VDEV_VPDO) {
		auto vpdo = (vpdo_dev_t*)vdev;
		status = RtlStringCbPrintfW(id_dev, str_sz, str, vpdo->vendor, vpdo->product);
	} else {
		RtlCopyMemory(id_dev, str, str_sz);
	}

	if (status == STATUS_SUCCESS) {
		*result = id_dev;
	}

	return status;
}

NTSTATUS setup_hw_ids(PWCHAR *result, bool *subst_result, vdev_t *vdev, IRP*)
{
	NTSTATUS status = STATUS_SUCCESS;

	LPCWSTR str = vdev_hwids[vdev->type];
	if (!str) {
		return STATUS_NOT_SUPPORTED;
	}

	size_t str_sz = vdev_hwids_size[vdev->type];

	auto ids_hw = (PWCHAR)ExAllocatePoolWithTag(PagedPool, str_sz, USBIP_VHCI_POOL_TAG);
	if (!ids_hw) {
		Trace(TRACE_LEVEL_ERROR, "%!vdev_type_t!: query hw ids: out of memory", vdev->type);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	*subst_result = vdev->type == VDEV_VPDO;

	if (*subst_result) {
		vpdo_dev_t * vpdo = (vpdo_dev_t *)vdev;
		status = RtlStringCbPrintfW(ids_hw, str_sz, str,
			vpdo->vendor, vpdo->product, vpdo->revision,
			vpdo->vendor, vpdo->product);
	} else {
		RtlCopyMemory(ids_hw, str, str_sz);
	}

	if (status == STATUS_SUCCESS) {
		*result = ids_hw;
	}

	return status;
}

/*
Instance ID

An instance ID is a device identification string that distinguishes a device
from other devices of the same type on a computer. An instance ID contains
serial number information, if supported by the underlying bus, or some kind
of location information. The string cannot contain any "\" characters;
otherwise, the generic format of the string is bus-specific.

The number of characters of an instance ID, excluding a NULL-terminator, must be
less than MAX_DEVICE_ID_LEN. In addition, when an instance ID is concatenated
to a device ID to create a device instance ID, the lengths of the device ID
and the instance ID are further constrained by the maximum possible length
of a device instance ID.

The UniqueID member of the DEVICE_CAPABILITIES structure for a device indicates
if a bus-supplied instance ID is unique across the system, as follows:
* If UniqueID is FALSE, the bus-supplied instance ID for a device is unique
only to the device's bus. The Plug and Play (PnP) manager modifies
the bus-supplied instance ID, and combines it with the corresponding device ID,
to create a device instance ID that is unique in the system.
* If UniqueID is TRUE, the device instance ID, formed from the bus-supplied
device ID and instance ID, uniquely identifies a device in the system.

An instance ID is persistent across system restarts.
*/
NTSTATUS setup_inst_id_or_serial(PWCHAR *result, bool*, vdev_t *vdev, IRP*, bool want_serial)
{
	if (vdev->type != VDEV_VPDO) {
		return STATUS_NOT_SUPPORTED;
	}

	const size_t max_wchars = MAX_VHCI_SERIAL_ID + 1;
	//	static_assert(MAX_VHCI_SERIAL_ID <= MAX_DEVICE_ID_LEN);

	vpdo_dev_t *vpdo = (vpdo_dev_t*)vdev;

	PWSTR str = (PWSTR)ExAllocatePoolWithTag(PagedPool, max_wchars*sizeof(*str), USBIP_VHCI_POOL_TAG);
	if (!str) {
		Trace(TRACE_LEVEL_ERROR, "vpdo: %s: out of memory", want_serial ? "DeviceSerialNumber" : "InstanceID");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	auto serial = vpdo->serial_usr ? vpdo->serial_usr :
		vpdo->serial ? vpdo->serial :
		L"";

	NTSTATUS status = status = *serial || want_serial ?
		RtlStringCchCopyW(str, max_wchars, serial) :
		RtlStringCchPrintfW(str, max_wchars, L"%04hx", vpdo->port);

	if (status == STATUS_SUCCESS) {
		*result = str;
	}

	return status;
}

NTSTATUS setup_compat_ids(PWCHAR *result, bool *subst_result, vdev_t *vdev, IRP*)
{
	if (vdev->type != VDEV_VPDO) {
		return STATUS_NOT_SUPPORTED;
	}

	const wchar_t fmt[] =
		L"USB\\Class_%02hhx&SubClass_%02hhx&Prot_%02hhx;" // 33 chars after formatting
		L"USB\\Class_%02hhx&SubClass_%02hhx;" // 25 chars after formatting
		L"USB\\Class_%02hhx;"; // 13 chars after formatting

	const wchar_t comp[] = L"USB\\COMPOSITE\0";
	const size_t max_wchars = 33 + 25 + 13 + ARRAYSIZE(comp);

	auto vpdo = (vpdo_dev_t*)vdev;

	PWSTR ids_compat = (PWSTR)ExAllocatePoolWithTag(PagedPool, max_wchars*sizeof(*ids_compat), USBIP_VHCI_POOL_TAG);
	if (!ids_compat) {
		Trace(TRACE_LEVEL_ERROR, "vpdo: query compatible id: out of memory");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	NTSTRSAFE_PWSTR dest_end = nullptr;
	size_t remaining = 0;

	NTSTATUS status = RtlStringCchPrintfExW(ids_compat, max_wchars, &dest_end, &remaining, 0, fmt,
		vpdo->usbclass, vpdo->subclass, vpdo->protocol,
		vpdo->usbclass, vpdo->subclass,
		vpdo->usbclass);

	if (status == STATUS_SUCCESS) {
		*result = ids_compat;
		*subst_result = true;
		if (is_composite(vpdo)) {
			NT_ASSERT(remaining == ARRAYSIZE(comp));
			RtlCopyMemory(dest_end, comp, sizeof(comp));
		}
	}

	return status;
}

} // namespace


/*
 * On success, a driver sets Irp->IoStatus.Information to a WCHAR pointer
 * that points to the requested information. On error, a driver sets
 * Irp->IoStatus.Information to zero.
 */
PAGEABLE NTSTATUS pnp_query_id(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	NTSTATUS status = STATUS_NOT_SUPPORTED;

	PWCHAR result = nullptr;
	bool subst_result = false;

	BUS_QUERY_ID_TYPE type = irpstack->Parameters.QueryId.IdType;

	switch (type) {
	case BusQueryDeviceID:
		status = setup_device_id(&result, &subst_result, vdev, irp);
		break;
	case BusQueryInstanceID:
		status = setup_inst_id_or_serial(&result, &subst_result, vdev, irp, false);
		break;
	case BusQueryHardwareIDs:
		status = setup_hw_ids(&result, &subst_result, vdev, irp);
		break;
	case BusQueryCompatibleIDs:
		status = setup_compat_ids(&result, &subst_result, vdev, irp);
		break;
	case BusQueryDeviceSerialNumber:
		status = setup_inst_id_or_serial(&result, &subst_result, vdev, irp, true);
		break;
	case BusQueryContainerID:
		break;
	}

	if (status == STATUS_SUCCESS) {
		Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!: %!BUS_QUERY_ID_TYPE!: %S", vdev->type, type, result);
		if (subst_result) {
			subst_char(result, L';', L'\0');
		}
	} else {
		Trace(TRACE_LEVEL_WARNING, "%!vdev_type_t!: %!BUS_QUERY_ID_TYPE!: %!STATUS!", vdev->type, type, status);
		if (result) {
			ExFreePoolWithTag(result, USBIP_VHCI_POOL_TAG);
			result = nullptr;
		}
	}

	irp->IoStatus.Information = (ULONG_PTR)result;
	return irp_done(irp, status);
}