/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2022-2023 Vadym Hrynchyshyn
 */

#include "common.h"

#include <usbip\proto_op.h>
#include <spdlog\spdlog.h>

namespace
{

constexpr auto &fmt_name_val = "{:20} = {}"; // name is left aligned
constexpr auto &fmt_name_hex = "{:20} = {:#x}";

struct speed_string 
{
        usb_device_speed val;
        const char *speed;
	const char *desc;
};

const speed_string speed_strings[] = 
{
        { USB_SPEED_UNKNOWN, "unknown", "Unknown Speed"},
        { USB_SPEED_LOW,  "1.5", "Low Speed(1.5Mbps)"  },
        { USB_SPEED_FULL, "12",  "Full Speed(12Mbps)" },
        { USB_SPEED_HIGH, "480", "High Speed(480Mbps)" },
        { USB_SPEED_WIRELESS, "53.3-480", "Wireless" },
        { USB_SPEED_SUPER, "5000", "Super Speed(5000Mbps)" },
        { USB_SPEED_SUPER_PLUS, "10000", "Super Speed Plus(10 Gbit/s)" },
        {}
};

} // namespace


const char *usbip_speed_string(usb_device_speed speed)
{
        for (auto &i : speed_strings) {
                if (i.val == speed) {
                        return i.desc;
                }
        }

	return "Unknown Speed";
}

void dump_usb_interface(const UsbIds &ids, usbip_usb_interface *uinf)
{
	auto csp = usbip_names_get_class(ids, uinf->bInterfaceClass, uinf->bInterfaceSubClass, uinf->bInterfaceProtocol);
        spdlog::debug(fmt_name_val, "Interface(C/SC/P)", csp);
}

void dump_usb_device(const UsbIds &ids, usbip_usb_device *udev)
{
        spdlog::debug(fmt_name_val, "path",  udev->path);
        spdlog::debug(fmt_name_val, "busid", udev->busid);

	auto str = usbip_names_get_class(ids, udev->bDeviceClass, udev->bDeviceSubClass, udev->bDeviceProtocol);
        spdlog::debug(fmt_name_val, "Device(C/SC/P)", str);

        spdlog::debug(fmt_name_hex, "bcdDevice", udev->bcdDevice);

	str = usbip_names_get_product(ids, udev->idVendor, udev->idProduct);
        spdlog::debug(fmt_name_val, "Vendor/Product", str);

        spdlog::debug(fmt_name_val, "bNumConfigurations", udev->bNumConfigurations);
        spdlog::debug(fmt_name_val, "bNumInterfaces", udev->bNumInterfaces);

        str = usbip_speed_string(static_cast<usb_device_speed>(udev->speed));
        spdlog::debug(fmt_name_val, "speed", str);

        spdlog::debug(fmt_name_val, "busnum", udev->busnum);
        spdlog::debug(fmt_name_val, "devnum", udev->devnum);
}

std::string usbip_names_get_product(const UsbIds &ids, uint16_t vendor, uint16_t product)
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

std::string usbip_names_get_class(const UsbIds &ids, uint8_t class_, uint8_t subclass, uint8_t protocol)
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
