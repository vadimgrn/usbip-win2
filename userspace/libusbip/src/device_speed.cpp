/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device_speed.h"
#include <cassert>

USB_DEVICE_SPEED usbip::win_speed(usb_device_speed speed) noexcept
{
        switch (speed) {
        case USB_SPEED_SUPER_PLUS:
        case USB_SPEED_SUPER:
                return UsbSuperSpeed;
        case USB_SPEED_WIRELESS:
        case USB_SPEED_HIGH:
                return UsbHighSpeed;
        case USB_SPEED_FULL:
                return UsbFullSpeed;
        case USB_SPEED_LOW: 
        case USB_SPEED_UNKNOWN:
                return UsbLowSpeed;
        }

        assert(!"win_speed");
        return UsbLowSpeed;
}

