/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "consts.h"
#include <basetsd.h>

 /*
  * Declarations from tools/usb/usbip/src/usbip_network.h
  */

namespace usbip
{

#include <PSHPACK1.H>

struct usbip_usb_interface 
{
        UINT8 bInterfaceClass;
        UINT8 bInterfaceSubClass;
        UINT8 bInterfaceProtocol;
        UINT8 padding; // alignment
};

struct usbip_usb_device 
{
        char path[DEV_PATH_MAX];
        char busid[BUS_ID_SIZE];

        UINT32 busnum;
        UINT32 devnum;
        UINT32 speed; // enum usb_device_speed

        UINT16 idVendor;
        UINT16 idProduct;
        UINT16 bcdDevice; // Device Release Number

        UINT8 bDeviceClass;
        UINT8 bDeviceSubClass;
        UINT8 bDeviceProtocol;

        UINT8 bConfigurationValue;
        
        UINT8 bNumConfigurations;
        UINT8 bNumInterfaces;
};

/* 
 * Common header
 */
struct op_common 
{
        UINT16 version; // USBIP_VERSION
        UINT16 code;
        UINT32 status; // op_status_t, for reply
};

struct op_import_request
{
        char busid[BUS_ID_SIZE];
};

struct op_import_reply
{
        usbip_usb_device udev;
};

struct op_devlist_request 
{
        UINT32 _reserved; // struct or union must have at leat one member in MSC
};

struct op_devlist_reply 
{
        UINT32 ndev;
        // followed by reply_extra[]
};

struct op_devlist_reply_extra 
{
        usbip_usb_device udev;
        // followed by usbip_usb_interface uinf[]
};

#include <POPPACK.H>


enum : UINT16 // op_common.code
{
        OP_REQUEST = 0x80 << 8,
        OP_REPLY = 0x00 << 8,

        // import a remote USB device
        OP_IMPORT = 3,
        OP_REQ_IMPORT = OP_REQUEST | OP_IMPORT,
        OP_REP_IMPORT = OP_REPLY | OP_IMPORT,

        // retrieve the list of exported USB devices
        OP_DEVLIST = 5,
        OP_REQ_DEVLIST = OP_REQUEST | OP_DEVLIST,
        OP_REP_DEVLIST = OP_REPLY | OP_DEVLIST,
};

inline void byteswap(usbip_usb_interface&) {} // nothing to do
void byteswap(usbip_usb_device &d);

void byteswap(op_common &c);

inline void byteswap(op_import_request&) {} // nothing to do
inline void byteswap(op_import_reply &r) { byteswap(r.udev); }

inline void byteswap(op_devlist_request&) {} // nothing to do
void byteswap(op_devlist_reply &r);
inline void byteswap(op_devlist_reply_extra &r) { byteswap(r.udev); }

} // namespace usbip
