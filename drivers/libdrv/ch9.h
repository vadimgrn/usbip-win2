/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <ntdef.h>
#include <wdm.h>
#include <usb.h>

enum { // <uapi/linux/usb/ch9.h>

/*
 * USB directions
 * This bit flag is used in endpoint descriptors' bEndpointAddress field.
 * It's also one of three fields in control requests bRequestType.
 */
	USB_DIR_OUT,		/* to device */
	USB_DIR_IN = 0x80,	/* to host */

//	USB types, the second of three bRequestType fields
	USB_TYPE_MASK     = 0x03 << 5,
	USB_TYPE_STANDARD = 0x00 << 5,
	USB_TYPE_CLASS    = 0x01 << 5,
	USB_TYPE_VENDOR	  = 0x02 << 5,
	USB_TYPE_RESERVED = 0x03 << 5,

//	USB recipients, the third of three bRequestType fields
	USB_RECIP_MASK      = 0x1f,
	USB_RECIP_DEVICE    = 0x00,
	USB_RECIP_INTERFACE = 0x01,
	USB_RECIP_ENDPOINT  = 0x02,
	USB_RECIP_OTHER     = 0x03,
	USB_RECIP_PORT      = 0x04,
	USB_RECIP_RPIPE     = 0x05
};

/*
 * Audio extension, these two are _only_ in audio endpoints.
 */
struct USB_ENDPOINT_DESCRIPTOR_AUDIO : USB_ENDPOINT_DESCRIPTOR 
{
	UCHAR bRefresh;
	UCHAR bSynchAddress;
};
static_assert(sizeof(USB_ENDPOINT_DESCRIPTOR_AUDIO) == sizeof(USB_ENDPOINT_DESCRIPTOR) + 2);

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
	return  epd.bLength == sizeof(epd) &&
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

inline auto operator ==(const USB_DEVICE_DESCRIPTOR &a, const USB_DEVICE_DESCRIPTOR &b)
{
	return reinterpret_cast<const USB_COMMON_DESCRIPTOR&>(a) == reinterpret_cast<const USB_COMMON_DESCRIPTOR&>(b);
}

inline auto operator !=(const USB_DEVICE_DESCRIPTOR &a, const USB_DEVICE_DESCRIPTOR &b)
{
	return !(a == b);
}

inline auto operator ==(const USB_CONFIGURATION_DESCRIPTOR &a, const USB_CONFIGURATION_DESCRIPTOR &b)
{
	return a.wTotalLength == b.wTotalLength && RtlEqualMemory(&a, &b, b.wTotalLength);
}

inline auto operator !=(const USB_CONFIGURATION_DESCRIPTOR &a, const USB_CONFIGURATION_DESCRIPTOR &b)
{
	return !(a == b);
}

inline auto operator ==(const USB_ENDPOINT_DESCRIPTOR &a, const USB_ENDPOINT_DESCRIPTOR &b)
{
	return reinterpret_cast<const USB_COMMON_DESCRIPTOR&>(a) == reinterpret_cast<const USB_COMMON_DESCRIPTOR&>(b);
}

inline auto operator !=(const USB_ENDPOINT_DESCRIPTOR &a, const USB_ENDPOINT_DESCRIPTOR &b)
{
	return !(a == b);
}
