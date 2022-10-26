#pragma once

#include <ntdef.h>
#include <usb.h>

/**
 * @return 0 to 15
 */
constexpr auto usb_endpoint_num(const USB_ENDPOINT_DESCRIPTOR &epd)
{
	return epd.bEndpointAddress & USB_ENDPOINT_ADDRESS_MASK;
}

constexpr auto usb_endpoint_type(const USB_ENDPOINT_DESCRIPTOR &epd)
{
	static_assert(USB_ENDPOINT_TYPE_CONTROL == UsbdPipeTypeControl);
	static_assert(USB_ENDPOINT_TYPE_ISOCHRONOUS == UsbdPipeTypeIsochronous);
	static_assert(USB_ENDPOINT_TYPE_BULK == UsbdPipeTypeBulk);
	static_assert(USB_ENDPOINT_TYPE_INTERRUPT == UsbdPipeTypeInterrupt);

	return static_cast<USBD_PIPE_TYPE>(epd.bmAttributes & USB_ENDPOINT_TYPE_MASK);
}

constexpr bool usb_endpoint_dir_in(const USB_ENDPOINT_DESCRIPTOR &epd)
{
	return USB_ENDPOINT_DIRECTION_IN(epd.bEndpointAddress);
}

constexpr bool usb_endpoint_dir_out(const USB_ENDPOINT_DESCRIPTOR &epd)
{
	return USB_ENDPOINT_DIRECTION_OUT(epd.bEndpointAddress);
}

/*
 * Default control pipe doesn't have descriptor, but zeroed descriptor 
 * has bEndpointAddress|bmAttributes that match expectations.
 */
constexpr auto usb_default_control_pipe(const USB_ENDPOINT_DESCRIPTOR &epd)
{
	return  epd.bLength >= sizeof(epd) &&
		epd.bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE &&
		epd.bEndpointAddress == USB_DEFAULT_ENDPOINT_ADDRESS &&
		usb_endpoint_type(epd) == UsbdPipeTypeControl;
}

constexpr USB_ENDPOINT_DESCRIPTOR EP0{ sizeof(EP0), USB_ENDPOINT_DESCRIPTOR_TYPE };
static_assert(usb_default_control_pipe(EP0));

inline auto operator ==(const USB_COMMON_DESCRIPTOR &a, const USB_COMMON_DESCRIPTOR &b)
{
	return a.bLength == b.bLength && RtlEqualMemory(&a, &b, b.bLength);
}

inline auto operator !=(const USB_COMMON_DESCRIPTOR &a, const USB_COMMON_DESCRIPTOR &b)
{
	return !(a == b);
}

inline auto operator ==(const USB_CONFIGURATION_DESCRIPTOR &a, const USB_CONFIGURATION_DESCRIPTOR &b)
{
	return  a.wTotalLength == b.wTotalLength &&
		RtlEqualMemory(&a, &b, b.wTotalLength);
}

inline auto operator !=(const USB_CONFIGURATION_DESCRIPTOR &a, const USB_CONFIGURATION_DESCRIPTOR &b)
{
	return !(a == b);
}
