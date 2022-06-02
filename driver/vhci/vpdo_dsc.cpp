#include "vpdo_dsc.h"
#include "trace.h"
#include "vpdo_dsc.tmh"

#include "vhci.h"
#include "dev.h"
#include "usbip_proto.h"
#include "irp.h"
#include "usbdsc.h"
#include "internal_ioctl.h"

#include <ntstrsafe.h>

namespace
{

/*
 * Calls from DISPATCH_LEVEL.
 */
auto copy_wstring(const USB_STRING_DESCRIPTOR *sd, const char *what)
{
	UCHAR cch = (sd->bLength - sizeof(USB_COMMON_DESCRIPTOR))/sizeof(*sd->bString) + 1;
	PWSTR str = (PWSTR)ExAllocatePool2(POOL_FLAG_NON_PAGED|POOL_FLAG_UNINITIALIZED, cch*sizeof(*str), USBIP_VHCI_POOL_TAG);

	if (str) {
		[[maybe_unused]] auto err = RtlStringCchCopyNW(str, cch, sd->bString, cch - 1);
		NT_ASSERT(!err);
		Trace(TRACE_LEVEL_INFORMATION, "%s '%S'", what, str);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate memory");
	}

	return str;
}

/*
 * Calls from DISPATCH_LEVEL.
 */
void save_string(vpdo_dev_t *vpdo, const USB_DEVICE_DESCRIPTOR &dd, const USB_STRING_DESCRIPTOR &sd, UCHAR Index)
{
	NT_ASSERT(Index);
	NT_ASSERT(is_valid_dsc(&dd));

	struct {
		UCHAR idx;
		PWSTR &str;
		const char *name;
	} v[] = {
		{ dd.iManufacturer, vpdo->Manufacturer, "Manufacturer" },
		{ dd.iProduct, vpdo->Product, "Product" },
		{ dd.iSerialNumber, vpdo->SerialNumber, "SerialNumber" },
	};

	for (auto& [idx, str, name] : v) {
		if (idx == Index) {
			if (!str) {
				str = copy_wstring(&sd, name);
			}
			break;
		}
	}
} 

} // namespace


PAGEABLE NTSTATUS vpdo_get_dsc_from_nodeconn(vpdo_dev_t *vpdo, IRP*, USB_DESCRIPTOR_REQUEST &r, [[maybe_unused]] ULONG &size)
{
	PAGED_CODE();

	auto setup = (USB_DEFAULT_PIPE_SETUP_PACKET*)&r.SetupPacket;
	static_assert(sizeof(*setup) == sizeof(r.SetupPacket));

	void *dsc_data = nullptr;
	ULONG dsc_len = 0;

	switch (setup->wValue.HiByte) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		dsc_data = is_valid_dsc(&vpdo->descriptor) ? &vpdo->descriptor : nullptr;
		if (dsc_data) {
			dsc_len = vpdo->descriptor.bLength;
		}
		break;
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		dsc_data = vpdo->actconfig;
		if (dsc_data) {
			dsc_len = vpdo->actconfig->wTotalLength;
		}
		break;
	}

	return STATUS_NOT_IMPLEMENTED; // FIXME

/*
	if (!dsc_data) {
		return STATUS_NOT_IMPLEMENTED;
	}

	ULONG outlen = sizeof(r) + dsc_len;

	if (size < sizeof(r)) {
		size = outlen;
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto ncopy = size < outlen ? size - sizeof(r) : outlen;
	if (ncopy) {
		RtlCopyMemory(r.Data, dsc_data, ncopy);
	}

	if (ncopy == outlen) {
		size = outlen;
	}

	return STATUS_SUCCESS;
*/
}

/*
 * Configuration descriptor will be saved on usb request select configuration.
 * Calls from DISPATCH_LEVEL.
 */
void cache_descriptor(vpdo_dev_t *vpdo, const _URB_CONTROL_DESCRIPTOR_REQUEST &r, const USB_COMMON_DESCRIPTOR *dsc)
{
	NT_ASSERT(dsc->bLength > sizeof(*dsc));

	if (dsc->bDescriptorType == USB_STRING_DESCRIPTOR_TYPE) {
		auto sd = reinterpret_cast<const USB_STRING_DESCRIPTOR*>(dsc);
		if (r.Index && sd->bLength >= sizeof(*sd)) {
			save_string(vpdo, vpdo->descriptor, *sd, r.Index);
		}
	} else if (dsc->bDescriptorType == USB_DEVICE_DESCRIPTOR_TYPE) {
		auto dd = reinterpret_cast<const USB_DEVICE_DESCRIPTOR*>(dsc);
		if (!is_valid_dsc(dd)) {
			Trace(TRACE_LEVEL_ERROR, "Device descriptor is not initialized");
		} else if (!RtlEqualMemory(&vpdo->descriptor, dd, sizeof(*dd))) {
			Trace(TRACE_LEVEL_WARNING, "Device descriptor is not the same");
			RtlCopyMemory(&vpdo->descriptor, dd, sizeof(*dd));
		}
	}
}
