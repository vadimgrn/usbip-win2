#pragma once

#include <ntddk.h>
#include <usbdi.h>

enum : UCHAR { MS_OS_DESC_STRING_IDX = 0xEE };

struct USB_OS_STRING_DESCRIPTOR : USB_COMMON_DESCRIPTOR
{
	WCHAR Signature[7]; // MSFT100
	UCHAR MS_VendorCode;
	UCHAR Pad;
};
static_assert(sizeof(USB_OS_STRING_DESCRIPTOR) == 18);

bool is_valid_dsc(const USB_DEVICE_DESCRIPTOR *d);
bool is_valid_dsc(const USB_CONFIGURATION_DESCRIPTOR *d);
bool is_valid_dsc(const USB_STRING_DESCRIPTOR *d);
bool is_valid_dsc(const USB_OS_STRING_DESCRIPTOR &d);

inline auto is_valid_dsc(const USB_DEVICE_DESCRIPTOR &d) { return is_valid_dsc(&d);  }
inline auto is_valid_dsc(const USB_CONFIGURATION_DESCRIPTOR &d) { return is_valid_dsc(&d);  }
inline auto is_valid_dsc(const USB_STRING_DESCRIPTOR &d) { return is_valid_dsc(&d);  }

inline auto dsc_next(USB_COMMON_DESCRIPTOR *d)
{
	void *end = d ? (char*)d + d->bLength : nullptr;
	return static_cast<USB_COMMON_DESCRIPTOR*>(end);
}

USB_COMMON_DESCRIPTOR *dsc_find_next(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_COMMON_DESCRIPTOR *from, int type);

inline auto dsc_find_next_intf(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, USB_INTERFACE_DESCRIPTOR *from)
{
	return (USB_INTERFACE_DESCRIPTOR*)dsc_find_next(dsc_conf, (USB_COMMON_DESCRIPTOR*)from, USB_INTERFACE_DESCRIPTOR_TYPE);
}

using dsc_for_each_ep_fn = bool(int, USB_ENDPOINT_DESCRIPTOR*, void*);

void *dsc_for_each_endpoint(
	USB_CONFIGURATION_DESCRIPTOR *dsc_conf, 
	USB_INTERFACE_DESCRIPTOR *dsc_intf, 
	dsc_for_each_ep_fn *func, 
	void *data);

USB_INTERFACE_DESCRIPTOR *dsc_find_intf(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, UCHAR intf_num, UCHAR alt_setting);
int get_intf_num_altsetting(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, UCHAR intf_num);
