/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 * Copyright (C) 2005 - 2007 Takahiro Hirofuchi
 */

#include "strings.h"

#include <libusbip\src\usb_ids.h>
#include <format>

namespace
{

auto &fmt_name_val = "{:20} = {}"; // name is left aligned
auto &fmt_name_hex = "{:20} = {:#x}";

} // namespace


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

         return speed >= 0 && speed < ARRAYSIZE(names) ? names[speed] : "Unknown Speed";
}

std::string usbip::get_product(const UsbIds &ids, uint16_t vendor, uint16_t product)
{
        auto [vend, prod] = ids.find_product(vendor, product);

        if (prod.empty()) {
		prod = "unknown product";
        }

	if (vend.empty()) {
		vend = "unknown vendor";
        }

	return std::format("{} : {} ({:04x}:{:04x})", vend, prod, vendor, product);
}

std::string usbip::get_class(const UsbIds &ids, uint8_t class_, uint8_t subclass, uint8_t protocol)
{
	if (!(class_ && subclass && protocol)) {
		return "(Defined at Interface level) (00/00/00)";
	}

	auto [c, s, p] = ids.find_class_subclass_proto(class_, subclass, protocol);

        if (c.empty()) {
                c = "?";
        }
        
	if (s.empty()) {
		s = "?";
        }

        if (p.empty()) {
                p = "?";
        }

	return std::format("{}/{}/{} ({:02x}/{:02x}/{:02x})", c, s, p, class_, subclass, protocol);
}
