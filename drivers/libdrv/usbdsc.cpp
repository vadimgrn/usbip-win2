/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbdsc.h"

/*
 * USBD_ParseDescriptors requires PASSIVE_LEVEL.
 * @see reactos\drivers\usb\usbd\usbd.c
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
USB_COMMON_DESCRIPTOR* libdrv::find_next(
        _In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_ LONG type, _In_opt_ USB_COMMON_DESCRIPTOR *cur)
{
        if (!(cfg && is_valid(*cfg))) {
                return nullptr;
        }

        const auto* const cfg_bytes = reinterpret_cast<char*>(cfg);
        const auto* const end_bytes = cfg_bytes + cfg->wTotalLength;

        cur = cur ? next(cur) : reinterpret_cast<USB_COMMON_DESCRIPTOR*>(cfg);

        NT_ASSERT(reinterpret_cast<char*>(cur) >= cfg_bytes);
        NT_ASSERT(reinterpret_cast<char*>(cur) <= end_bytes);

        for ( ; reinterpret_cast<char*>(cur) + sizeof(*cur) <= end_bytes; cur = next(cur)) {
                
                if (!is_valid(*cur)) [[unlikely]] {
                        break; 
                }

                if (reinterpret_cast<char*>(cur) + cur->bLength > end_bytes) [[unlikely]] {
                        break;
                }

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

