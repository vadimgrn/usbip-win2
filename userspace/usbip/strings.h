/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn
 */

#pragma once

#include <wtypes.h>
#include <usbspec.h>

#include <string>

namespace usbip
{

class UsbIds;
std::string get_product(const UsbIds &ids, uint16_t vendor, uint16_t product);
std::string get_class(const UsbIds &ids, uint8_t class_, uint8_t subclass, uint8_t protocol);

const char *get_speed_str(USB_DEVICE_SPEED speed) noexcept;

} // namespace usbip
