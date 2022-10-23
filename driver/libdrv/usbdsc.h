#pragma once

#include <ntddk.h>
#include <usbdi.h>

namespace usbdlib
{

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

inline auto next_descr(USB_COMMON_DESCRIPTOR *d)
{
	void *end = d ? (char*)d + d->bLength : nullptr;
	return static_cast<USB_COMMON_DESCRIPTOR*>(end);
}

USB_COMMON_DESCRIPTOR *find_next_descr(
	USB_CONFIGURATION_DESCRIPTOR *cfg, LONG type, USB_COMMON_DESCRIPTOR *prev = nullptr);

USB_INTERFACE_DESCRIPTOR *find_next_intf(
	USB_CONFIGURATION_DESCRIPTOR *cfg, USB_INTERFACE_DESCRIPTOR *prev = nullptr, 
	LONG intf_num = -1, LONG alt_setting = -1, LONG _class = -1, LONG subclass = -1, LONG proto = -1);

int get_intf_num_altsetting(USB_CONFIGURATION_DESCRIPTOR *cfg, LONG intf_num);

USB_INTERFACE_DESCRIPTOR* find_intf(USB_CONFIGURATION_DESCRIPTOR *cfg, const USB_ENDPOINT_DESCRIPTOR &epd);

using for_each_iface_fn = NTSTATUS (USB_INTERFACE_DESCRIPTOR&, void*);
NTSTATUS for_each_intf(USB_CONFIGURATION_DESCRIPTOR *cfg, for_each_iface_fn func, void *data);

using for_each_ep_fn = NTSTATUS (int, USB_ENDPOINT_DESCRIPTOR&, void*);
NTSTATUS for_each_endp(USB_CONFIGURATION_DESCRIPTOR *cfg, USB_INTERFACE_DESCRIPTOR *iface, for_each_ep_fn func, void *data);

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

inline auto operator == (const USB_ENDPOINT_DESCRIPTOR &a, const USB_ENDPOINT_DESCRIPTOR &b)
{
	return a.bLength == b.bLength && RtlEqualMemory(&a, &b, b.bLength);
}

inline auto operator != (const USB_ENDPOINT_DESCRIPTOR &a, const USB_ENDPOINT_DESCRIPTOR &b)
{
	return !(a == b);
}

} // namespace usbdlib
