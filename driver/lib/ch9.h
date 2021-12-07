#pragma once

/*
 * USB directions
 *
 * This bit flag is used in endpoint descriptors' bEndpointAddress field.
 * It's also one of three fields in control requests bRequestType.
 */
enum { 
	USB_DIR_OUT,		/* to device */
	USB_DIR_IN = 0x80	/* to host */
};

/*
 * USB types, the second of three bRequestType fields
 */
enum {
	USB_TYPE_MASK     = 0x03 << 5,
	USB_TYPE_STANDARD = 0x00 << 5,
	USB_TYPE_CLASS    = 0x01 << 5,
	USB_TYPE_VENDOR	  = 0x02 << 5,
	USB_TYPE_RESERVED = 0x03 << 5
};

/*
 * USB recipients, the third of three bRequestType fields
 */
enum {
	USB_RECIP_MASK      = 0x1f,
	USB_RECIP_DEVICE    = 0x00,
	USB_RECIP_INTERFACE = 0x01,
	USB_RECIP_ENDPOINT  = 0x02,
	USB_RECIP_OTHER     = 0x03,
	USB_RECIP_PORT      = 0x04,
	USB_RECIP_RPIPE     = 0x05
};