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
	_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_ LONG type, _In_opt_ USB_COMMON_DESCRIPTOR *cur)
{
	NT_ASSERT(cfg);
	auto end = reinterpret_cast<USB_COMMON_DESCRIPTOR*>(reinterpret_cast<char*>(cfg) + cfg->wTotalLength);

	cur = cur ? next(cur) : reinterpret_cast<USB_COMMON_DESCRIPTOR*>(cfg);

	NT_ASSERT(cur >= static_cast<void*>(cfg));
	NT_ASSERT(cur <= end);

	for (USB_COMMON_DESCRIPTOR *nxt; 
	     cur + 1 <= end && is_valid(*cur) && (nxt = next(cur)) <= end; cur = nxt) {
		if (cur->bDescriptorType == type) {
			return cur;
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

