#pragma once

#include <ntddk.h>
#include <usb.h>

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

constexpr auto is_valid(_In_ const USB_DEVICE_DESCRIPTOR &d)
{
	return  d.bLength == sizeof(d) && 
		d.bDescriptorType == USB_DEVICE_DESCRIPTOR_TYPE;
}

constexpr auto is_valid(_In_ const USB_CONFIGURATION_DESCRIPTOR &d)
{
	return  d.bLength == sizeof(d) &&
		d.bDescriptorType == USB_CONFIGURATION_DESCRIPTOR_TYPE &&
		d.wTotalLength > d.bLength;
}

constexpr auto is_valid(_In_ const USB_INTERFACE_DESCRIPTOR &d)
{
	return  d.bLength == sizeof(d) &&
		d.bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE;
}

constexpr auto is_valid(_In_ const USB_STRING_DESCRIPTOR &d)
{
	return  d.bLength >= sizeof(USB_COMMON_DESCRIPTOR) && // string length can be zero
		d.bDescriptorType == USB_STRING_DESCRIPTOR_TYPE;
}

bool is_valid(_In_ const USB_OS_STRING_DESCRIPTOR &d);

inline auto next(_In_ USB_COMMON_DESCRIPTOR *d)
{
	NT_ASSERT(d);
	auto next = reinterpret_cast<char*>(d) + d->bLength;
	return reinterpret_cast<USB_COMMON_DESCRIPTOR*>(next);
}

inline auto next(_In_ const USBD_INTERFACE_INFORMATION *d)
{
	NT_ASSERT(d);
	auto next = reinterpret_cast<const char*>(d) + d->Length;
	return reinterpret_cast<const USBD_INTERFACE_INFORMATION*>(next);
}

USB_COMMON_DESCRIPTOR *find_next_descr(
	_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_ LONG type, _In_opt_ USB_COMMON_DESCRIPTOR *prev = nullptr);

USB_INTERFACE_DESCRIPTOR *find_next_intf(
	_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_opt_ USB_INTERFACE_DESCRIPTOR *prev = nullptr, 
	_In_ LONG intf_num = -1, _In_ LONG alt_setting = -1, 
	_In_ LONG _class = -1, _In_ LONG subclass = -1, _In_ LONG proto = -1);

int get_intf_num_altsetting(_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_ LONG intf_num);

USB_INTERFACE_DESCRIPTOR* find_intf(_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_ const USB_ENDPOINT_DESCRIPTOR &epd);

using for_each_intf_alt_fn = NTSTATUS (_In_ USB_INTERFACE_DESCRIPTOR&, _In_opt_ void*);
NTSTATUS for_each_intf_alt(_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_ for_each_intf_alt_fn func, _In_opt_ void *data);

using for_each_ep_fn = NTSTATUS (int, USB_ENDPOINT_DESCRIPTOR&, void*);
NTSTATUS for_each_endp(
	_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_ USB_INTERFACE_DESCRIPTOR *ifd, 
	_In_ for_each_ep_fn func, _In_ void *data);

inline auto get_string(_In_ USB_STRING_DESCRIPTOR &d)
{
	USHORT len = d.bLength - sizeof(USB_COMMON_DESCRIPTOR);
	return UNICODE_STRING{ len, len, d.bString };
}

inline void terminate_by_zero(_Inout_ USB_STRING_DESCRIPTOR &d)
{
	*reinterpret_cast<wchar_t*>((char*)&d + d.bLength) = L'\0';
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool is_composite(_In_ const USB_DEVICE_DESCRIPTOR &dd, _In_ const USB_CONFIGURATION_DESCRIPTOR &cd);

} // namespace usbdlib
