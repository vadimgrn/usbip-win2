/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbip/ch9.h>

#include <wtypes.h>
#include <usbspec.h>

namespace usbip
{

USB_DEVICE_SPEED win_speed(usb_device_speed speed) noexcept;

} // namespace usbip