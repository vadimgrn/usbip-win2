#pragma once

enum { 
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
 * bcdUSB field of USB device descriptor.
 * The value is in binary coded decimal with a format of 0xJJMN.
 */
enum {
	bcdUSB10 = 0x0100,
	bcdUSB11 = 0x0110,
	bcdUSB20 = 0x0200,
	bcdUSB30 = 0x0300,
	bcdUSB31 = 0x0310
};
