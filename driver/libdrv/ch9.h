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
