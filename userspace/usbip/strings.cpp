/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "strings.h"

#include <libusbip/src/usb_ids.h>
#include <format>
#include <utility>

namespace
{

constexpr auto &str_zero_copy = "zero-copy";
constexpr auto &str_low_latency = "low-latency";

} // namespace

const char* usbip::to_string(receive_mode mode) noexcept
{
        return mode == receive_mode::low_latency ? str_low_latency : str_zero_copy;
}

const char* usbip::get_speed_str(USB_DEVICE_SPEED speed) noexcept
{
         const char *names[] { 
                 "Low Speed(1.5Mbps)", 
                 "Full Speed(12Mbps)", 
                 "High Speed(480Mbps)", 
                 "Super Speed(5000Mbps)",
         };

         static_assert(UsbLowSpeed == 0);
         static_assert(UsbFullSpeed == 1);
         static_assert(UsbHighSpeed == 2);
         static_assert(UsbSuperSpeed == 3);

         auto idx = std::to_underlying(speed);
         return idx >= 0 && idx < std::ssize(names) ? names[idx] : "Unknown Speed";
}

std::string usbip::get_product(const UsbIds &ids, uint16_t vendor, uint16_t product)
{
        auto [vend, prod] = ids.find_product(vendor, product);
        return std::format("{} : {} ({:04x}:{:04x})",
                vend.empty() ? "unknown vendor" : vend,
                prod.empty() ? "unknown product" : prod,
                vendor, product);
}

std::string usbip::get_class(const UsbIds &ids, uint8_t class_, uint8_t subclass, uint8_t protocol)
{
	if (!(class_ || subclass || protocol)) {
		return "(Defined at Interface level) (00/00/00)";
	}

	auto [c, s, p] = ids.find_class_subclass_proto(class_, subclass, protocol);
	return std::format("{}/{}/{} ({:02x}/{:02x}/{:02x})",
                c.empty() ? "?" : c,
                s.empty() ? "?" : s,
                p.empty() ? "?" : p,
                class_, subclass, protocol);
}
