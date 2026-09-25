/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbip/ch9.h>

#include <wtypes.h>
#include <usbspec.h>

#include <optional>

namespace usbip
{

[[nodiscard]] constexpr std::optional<USB_DEVICE_SPEED> win_speed(usb_device_speed speed) noexcept
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
                return UsbLowSpeed;
        case USB_SPEED_UNKNOWN:
                break;
        }

        return std::nullopt;
}

} // namespace usbip
