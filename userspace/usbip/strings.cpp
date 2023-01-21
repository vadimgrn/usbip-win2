/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2022-2023 Vadym Hrynchyshyn
 */

#include "strings.h"

#include <usbip\proto_op.h>
#include <libusbip\usb_ids.h>

#include <spdlog\spdlog.h>

namespace
{

constexpr auto &fmt_name_val = "{:20} = {}"; // name is left aligned
constexpr auto &fmt_name_hex = "{:20} = {:#x}";

} // namespace


const char* usbip::get_speed_str(usb_device_speed speed)
{
        struct {
                usb_device_speed val;
                const char *speed;
                const char *desc;
        } const v[] = {
                { USB_SPEED_UNKNOWN, "unknown", "Unknown Speed"},
                { USB_SPEED_LOW,  "1.5", "Low Speed(1.5Mbps)"  },
                { USB_SPEED_FULL, "12",  "Full Speed(12Mbps)" },
                { USB_SPEED_HIGH, "480", "High Speed(480Mbps)" },
                { USB_SPEED_WIRELESS, "53.3-480", "Wireless" },
                { USB_SPEED_SUPER, "5000", "Super Speed(5000Mbps)" },
                { USB_SPEED_SUPER_PLUS, "10000", "Super Speed Plus(10 Gbit/s)" },
                {}
        };

        for (auto &i : v) {
                if (i.val == speed) {
                        return i.desc;
                }
        }

	return v[0].desc;
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
