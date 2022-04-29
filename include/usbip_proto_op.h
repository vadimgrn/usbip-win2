#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "usbip_api_consts.h"
#include <basetsd.h>

#include <PSHPACK1.H>

struct usbip_usb_interface 
{
        UINT8 bInterfaceClass;
        UINT8 bInterfaceSubClass;
        UINT8 bInterfaceProtocol;
        UINT8 padding;	/* alignment */
};

struct usbip_usb_device 
{
        char path[USBIP_DEV_PATH_MAX];
        char busid[USBIP_BUS_ID_SIZE];

        UINT32 busnum;
        UINT32 devnum;
        UINT32 speed;

        UINT16 idVendor;
        UINT16 idProduct;
        UINT16 bcdDevice;

        UINT8 bDeviceClass;
        UINT8 bDeviceSubClass;
        UINT8 bDeviceProtocol;
        UINT8 bConfigurationValue;
        UINT8 bNumConfigurations;
        UINT8 bNumInterfaces;
};


/* ---------------------------------------------------------------------- */
/* Common header for all the kinds of PDUs. */
struct op_common {
        UINT16 version;

#define OP_REQUEST	(0x80 << 8)
#define OP_REPLY	(0x00 << 8)
        UINT16 code;

        UINT32 status; /* op_code status (for reply) */
};

#define PACK_OP_COMMON(pack, op_common)  do {\
	usbip_net_pack_uint16_t(pack, &(op_common)->version);\
	usbip_net_pack_uint16_t(pack, &(op_common)->code);\
	usbip_net_pack_uint32_t(pack, &(op_common)->status);\
} while (0)

/* ---------------------------------------------------------------------- */
/* Dummy Code */
#define OP_UNSPEC	0x00
#define OP_REQ_UNSPEC	OP_UNSPEC
#define OP_REP_UNSPEC	OP_UNSPEC

/* ---------------------------------------------------------------------- */
/* Retrieve USB device information. (still not used) */
#define OP_DEVINFO	0x02
#define OP_REQ_DEVINFO	(OP_REQUEST | OP_DEVINFO)
#define OP_REP_DEVINFO	(OP_REPLY   | OP_DEVINFO)

/* ---------------------------------------------------------------------- */
/* Import a remote USB device. */
#define OP_IMPORT	0x03
#define OP_REQ_IMPORT	(OP_REQUEST | OP_IMPORT)
#define OP_REP_IMPORT   (OP_REPLY   | OP_IMPORT)

struct op_import_request {
        char busid[USBIP_BUS_ID_SIZE];
};

struct op_import_reply {
        struct usbip_usb_device udev;
//	struct usbip_usb_interface uinf[];
};

#define PACK_OP_IMPORT_REQUEST(pack, request)  do {\
} while (0)

#define PACK_OP_IMPORT_REPLY(pack, reply)  do {\
	usbip_net_pack_usb_device(pack, &(reply)->udev);\
} while (0)

/* ---------------------------------------------------------------------- */
/* Export a USB device to a remote host. */
#define OP_EXPORT	0x06
#define OP_REQ_EXPORT	(OP_REQUEST | OP_EXPORT)
#define OP_REP_EXPORT	(OP_REPLY   | OP_EXPORT)

struct op_export_request {
        struct usbip_usb_device udev;
};

struct op_export_reply {
        int returncode;
};


#define PACK_OP_EXPORT_REQUEST(pack, request)  do {\
	pack_usb_device(pack, &(request)->udev);\
} while (0)

#define PACK_OP_EXPORT_REPLY(pack, reply)  do {\
} while (0)

/* ---------------------------------------------------------------------- */
/* un-Export a USB device from a remote host. */
#define OP_UNEXPORT	0x07
#define OP_REQ_UNEXPORT	(OP_REQUEST | OP_UNEXPORT)
#define OP_REP_UNEXPORT	(OP_REPLY   | OP_UNEXPORT)

struct op_unexport_request {
        struct usbip_usb_device udev;
};

struct op_unexport_reply {
        int returncode;
};

#define PACK_OP_UNEXPORT_REQUEST(pack, request)  do {\
	pack_usb_device(pack, &(request)->udev);\
} while (0)

#define PACK_OP_UNEXPORT_REPLY(pack, reply)  do {\
} while (0)

/* ---------------------------------------------------------------------- */
/* Negotiate IPSec encryption key. (still not used) */
#define OP_CRYPKEY	0x04
#define OP_REQ_CRYPKEY	(OP_REQUEST | OP_CRYPKEY)
#define OP_REP_CRYPKEY	(OP_REPLY   | OP_CRYPKEY)

struct op_crypkey_request {
        /* 128bit key */
        UINT32 key[4];
};

struct op_crypkey_reply {
        UINT32 _reserved;
};


/* ---------------------------------------------------------------------- */
/* Retrieve the list of exported USB devices. */
#define OP_DEVLIST	0x05
#define OP_REQ_DEVLIST	(OP_REQUEST | OP_DEVLIST)
#define OP_REP_DEVLIST	(OP_REPLY   | OP_DEVLIST)

struct op_devlist_request {
        /* Struct or union must have at leat one member in MSC */
        UINT32 _reserved;
};

struct op_devlist_reply {
        UINT32 ndev;
        /* followed by reply_extra[] */
};

struct op_devlist_reply_extra {
        struct usbip_usb_device    udev;
        //	usbip_usb_interface uinf[];
};

#define PACK_OP_DEVLIST_REQUEST(pack, request)  do {\
} while (0)

#define PACK_OP_DEVLIST_REPLY(pack, reply)  do {\
	usbip_net_pack_uint32_t(pack, &(reply)->ndev);	\
} while (0)

#include <POPPACK.H>

void usbip_net_pack_uint32_t(int pack, UINT32 *num);
void usbip_net_pack_uint16_t(int pack, UINT16 *num);
void usbip_net_pack_usb_device(int pack, struct usbip_usb_device *udev);
void usbip_net_pack_usb_interface(int pack, struct usbip_usb_interface *uinf);

#ifdef __cplusplus
}
#endif
