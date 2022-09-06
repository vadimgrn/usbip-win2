#pragma once

#include <ntddk.h>
#include <usbdi.h>

enum : UCHAR { MS_OS_STRING_DESC_INDEX = 0xEE };

struct USB_OS_STRING_DESCRIPTOR : USB_COMMON_DESCRIPTOR
{
	WCHAR Signature[7]; // MSFT100
	UCHAR MS_VendorCode;
	UCHAR Pad;
};
static_assert(sizeof(USB_OS_STRING_DESCRIPTOR) == 18);

constexpr auto is_valid(const USB_COMMON_DESCRIPTOR &d) { return d.bLength >= sizeof(d); }
bool is_valid(const USB_DEVICE_DESCRIPTOR &d);
bool is_valid(const USB_CONFIGURATION_DESCRIPTOR &d);
bool is_valid(const USB_STRING_DESCRIPTOR &d);
bool is_valid(const USB_OS_STRING_DESCRIPTOR &d);

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

using for_each_ep_fn = NTSTATUS (int, const USB_ENDPOINT_DESCRIPTOR&, void*);
NTSTATUS for_each_endpoint(USB_CONFIGURATION_DESCRIPTOR *cfg, USB_INTERFACE_DESCRIPTOR *iface, for_each_ep_fn &func, void *data);

USB_INTERFACE_DESCRIPTOR *dsc_find_intf(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, UCHAR intf_num, UCHAR alt_setting);
int get_intf_num_altsetting(USB_CONFIGURATION_DESCRIPTOR *dsc_conf, UCHAR intf_num);

inline auto get_string(USB_STRING_DESCRIPTOR &d)
{
	USHORT len = d.bLength - sizeof(USB_COMMON_DESCRIPTOR);
	UNICODE_STRING s{ len, len, d.bString };
	return s;
}

inline void terminate_by_zero(_Inout_ USB_STRING_DESCRIPTOR &d)
{
	*reinterpret_cast<wchar_t*>((char*)&d + d.bLength) = L'\0';
}
