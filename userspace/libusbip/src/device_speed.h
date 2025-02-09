/*
 * Copyright (C) 2023 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <usbip/ch9.h>

#include <wtypes.h>
#include <usbspec.h>

namespace usbip
{

USB_DEVICE_SPEED win_speed(usb_device_speed speed) noexcept;

} // namespace usbip