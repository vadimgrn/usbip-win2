/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "ch9.h"

/*
 * Declarations from include/uapi/linux/usb/ch11.h
 */

/*
 * Hub request types
 */
enum { 
        USB_RT_HUB  = USB_TYPE_CLASS | USB_RECIP_DEVICE,
        USB_RT_PORT = USB_TYPE_CLASS | USB_RECIP_OTHER,
};

/*
 * Port feature numbers
 * See USB 2.0 spec Table 11-17
 */
enum {
        USB_PORT_FEAT_CONNECTION,
        USB_PORT_FEAT_ENABLE,
        USB_PORT_FEAT_SUSPEND, // L2 suspend
        USB_PORT_FEAT_OVER_CURRENT,
        USB_PORT_FEAT_RESET,
};
