/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/src/offsetof_ex.h>

#include <stddef.h>
#include <ntddk.h>
#include <usb.h>

#include <usbspec.h>

namespace libdrv
{

enum : UCHAR { MS_OS_STRING_DESC_INDEX = 0xEE };

struct USB_OS_STRING_DESCRIPTOR : USB_COMMON_DESCRIPTOR
{
	WCHAR Signature[7]; // MSFT100
	UCHAR MS_VendorCode;
	UCHAR Pad;
};
static_assert(sizeof(USB_OS_STRING_DESCRIPTOR) == 18);

constexpr auto usb_string_descr_size(UCHAR n)
{
        auto cb = offsetof_ex(USB_STRING_DESCRIPTOR, bString) + n*sizeof(*USB_STRING_DESCRIPTOR::bString);
        if (cb >= MAXIMUM_USB_STRING_LENGTH) [[unlikely]] {
                return UCHAR{};
        }
        return static_cast<UCHAR>(cb);
}
static_assert(usb_string_descr_size(0) == sizeof(USB_COMMON_DESCRIPTOR));
static_assert(usb_string_descr_size(126) + 1 == MAXIMUM_USB_STRING_LENGTH);

constexpr auto is_valid(_In_ const USB_COMMON_DESCRIPTOR &d)
{
	return d.bLength >= sizeof(d);
}

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

constexpr auto is_valid(_In_ const USB_ENDPOINT_DESCRIPTOR &d)
{
        return  d.bLength >= sizeof(d) && 
                d.bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE;
}

constexpr auto is_valid(_In_ const USB_STRING_DESCRIPTOR &d)
{
        return  d.bLength >= sizeof(USB_COMMON_DESCRIPTOR) && 
                d.bDescriptorType == USB_STRING_DESCRIPTOR_TYPE && 
                !(d.bLength % 2); // crucial check for UTF-16 character sizing
}

constexpr auto is_valid(_In_ const USB_INTERFACE_ASSOCIATION_DESCRIPTOR &d)
{
	return  d.bLength == sizeof(d) &&
		d.bDescriptorType == USB_INTERFACE_ASSOCIATION_DESCRIPTOR_TYPE;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool is_valid(_In_ const USB_OS_STRING_DESCRIPTOR &d);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto next(_In_ USB_COMMON_DESCRIPTOR *d)
{
	NT_ASSERT(d);
	void *next = reinterpret_cast<char*>(d) + d->bLength;
	return static_cast<USB_COMMON_DESCRIPTOR*>(next);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto next(_In_ const USB_COMMON_DESCRIPTOR *d)
{
	NT_ASSERT(d);
	const void *next = reinterpret_cast<const char*>(d) + d->bLength;
	return static_cast<const USB_COMMON_DESCRIPTOR*>(next);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto next(_In_ const USBD_INTERFACE_INFORMATION *d)
{
	NT_ASSERT(d);
	const void *next = reinterpret_cast<const char*>(d) + d->Length;
	return static_cast<const USBD_INTERFACE_INFORMATION*>(next);
}

/*
 * @param type of the usb descriptor - interface, endpoint, string
 * @param cur nullptr for the first iteration or result from the previous iteration
 * @return next found descriptor or nullptr
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
USB_COMMON_DESCRIPTOR *find_next(
	_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_ LONG type, _In_opt_ USB_COMMON_DESCRIPTOR *cur);

template<typename Desc>
struct descriptor_traits;

template<> struct descriptor_traits<USB_INTERFACE_DESCRIPTOR> {
	static constexpr LONG type = USB_INTERFACE_DESCRIPTOR_TYPE;
};
template<> struct descriptor_traits<USB_ENDPOINT_DESCRIPTOR> {
	static constexpr LONG type = USB_ENDPOINT_DESCRIPTOR_TYPE;
};
template<> struct descriptor_traits<USB_STRING_DESCRIPTOR> {
	static constexpr LONG type = USB_STRING_DESCRIPTOR_TYPE;
};
template<> struct descriptor_traits<USB_INTERFACE_ASSOCIATION_DESCRIPTOR> {
	static constexpr LONG type = USB_INTERFACE_ASSOCIATION_DESCRIPTOR_TYPE;
};

template<typename Desc>
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline Desc* find_next(
	_In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_opt_ void *cur = nullptr)
{
	auto c = static_cast<USB_COMMON_DESCRIPTOR*>(cur);
	while ((c = find_next(cfg, descriptor_traits<Desc>::type, c)) != nullptr) {
		if (auto d = reinterpret_cast<Desc*>(c); is_valid(*d)) {
			return d;
		}
	}
	return nullptr;
}

} // namespace libdrv
