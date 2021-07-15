#pragma once

#include "usbip_proto.h"

#include <ntdef.h>
#include <usbspec.h>

typedef USB_DEFAULT_PIPE_SETUP_PACKET	usb_cspkt_t;

enum usb_device_speed get_usb_speed(USHORT bcdUSB);
