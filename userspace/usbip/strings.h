/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/vhci.h>
#include <usbspec.h>

#include <format>
#include <string>

namespace usbip
{

class UsbIds;
std::string get_product(const UsbIds &ids, uint16_t vendor, uint16_t product);
std::string get_class(const UsbIds &ids, uint8_t class_, uint8_t subclass, uint8_t protocol);

const char *get_speed_str(USB_DEVICE_SPEED speed) noexcept;
const char *to_string(receive_mode mode) noexcept;
const char *to_string(isolation iso) noexcept;

} // namespace usbip

template <>
struct std::formatter<usbip::device_config> : std::formatter<std::string_view>
{
        auto format(const usbip::device_config &d, auto &ctx) const
        {
                if (d.iso_mode == usbip::isolation::user && !d.owner_sid.empty()) {
                        return std::format_to(ctx.out(), "{}:{}/{}, serial:{}, mode:{}, isolation:{}, owner:{}",
                                d.location.hostname, d.location.service, d.location.busid,
                                d.serial, usbip::to_string(d.recv_mode), usbip::to_string(d.iso_mode), d.owner_sid);
                }
                return std::format_to(ctx.out(), "{}:{}/{}, serial:{}, mode:{}, isolation:{}",
                        d.location.hostname, d.location.service, d.location.busid,
                        d.serial, usbip::to_string(d.recv_mode), usbip::to_string(d.iso_mode));
        }
};
