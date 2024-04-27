/*
 * Copyright (C) 2022 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbdsc.h"

/*
 * USBD_ParseDescriptors requires PASSIVE_LEVEL.
 * @see reactos\drivers\usb\usbd\usbd.c
 */
_IRQL_requires_same_
_IRQL_requires_(DISPATCH_LEVEL)
USB_COMMON_DESCRIPTOR* libdrv::find_next(
	_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_ LONG type, _In_opt_ USB_COMMON_DESCRIPTOR *prev)
{
	NT_ASSERT(cfg);
	void *end = reinterpret_cast<char*>(cfg) + cfg->wTotalLength;

	auto start = prev ? next(prev) : static_cast<void*>(cfg);
	NT_ASSERT(start >= cfg);
	NT_ASSERT(start <= end);

	for (auto d = static_cast<USB_COMMON_DESCRIPTOR*>(start); next(d) <= end; d = next(d)) {
		if (d->bDescriptorType == type) {
			return d;
		} else if (d->bLength < sizeof(*d)) { // invalid USB descriptor
			return nullptr;
		}
	}

	return nullptr;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool libdrv::is_valid(_In_ const USB_OS_STRING_DESCRIPTOR &d)
{
	return  d.bLength == sizeof(d) && 
		d.bDescriptorType == USB_STRING_DESCRIPTOR_TYPE && 
		RtlEqualMemory(d.Signature, L"MSFT100", sizeof(d.Signature));
}

