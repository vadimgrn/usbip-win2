/*
 * Copyright (C) 2021, 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usb_util.h"
#include "usbip_api_consts.h"

/*
 * The bcdUSB field reports the highest version of USB the device supports.
 * The value is in binary coded decimal with a format of 0xJJMN where
 * JJ is the major version number, M is the minor version number and N is the
 * sub minor version number.
 * e.g. USB 2.0 is reported as 0x0200, USB 1.1 as 0x0110 and USB 1.0 as 0x0100.
 *
 * <uapi/linux/usb/ch9.h>
 */
usb_device_speed get_usb_speed(USHORT bcdUSB)
{
	switch (bcdUSB) {
	case 0x0100:
		return USB_SPEED_LOW;
	case 0x0110:
		return USB_SPEED_FULL;
	case 0x0200:
		return USB_SPEED_HIGH;
	case 0x0250:
		return USB_SPEED_WIRELESS;
	case 0x0300:
		return USB_SPEED_SUPER;
	case 0x0310:
		return USB_SPEED_SUPER_PLUS;
	default:
		return USB_SPEED_LOW;
	}
}
