/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "usbip_common.h"
#include "usbip_util.h"
#include <usbip\proto_op.h>

#include <format>

bool usbip_use_stderr;
bool usbip_use_debug;
const char* usbip_progname;

#define DBG_UDEV_INTEGER(name)\
	dbg("%-20s = %x", #name, (int) udev->name)

#define DBG_UINF_INTEGER(name)\
	dbg("%-20s = %x", #name, (int) uinf->name)

namespace
{

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

struct portst_string 
{
        usbip_device_status status;
        const char *desc;
};

const portst_string portst_strings[] = 
{
        { SDEV_ST_AVAILABLE,	"Device Available" },
        { SDEV_ST_USED,		"Device in Use" },
        { SDEV_ST_ERROR,	"Device Error"},
        { VDEV_ST_NULL,		"Port Available"},
        { VDEV_ST_NOTASSIGNED,	"Port Initializing"},
        { VDEV_ST_USED,		"Port in Use"},
        { VDEV_ST_ERROR,	"Port Error"},
        {}
};

} // namespace


const char *usbip_status_string(usbip_device_status status)
{
        for (auto &i: portst_strings) {
                if (i.status == status) {
                        return i.desc;
                }
        }

	return "Unknown Status";
}

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
        dbg("%-20s = %s", "Interface(C/SC/P)", csp.c_str());
}

void dump_usb_device(const UsbIds &ids, usbip_usb_device *udev)
{
	dbg("%-20s = %s", "path",  udev->path);
	dbg("%-20s = %s", "busid", udev->busid);

	auto csp = usbip_names_get_class(ids, udev->bDeviceClass, udev->bDeviceSubClass, udev->bDeviceProtocol);
        dbg("%-20s = %s", "Device(C/SC/P)", csp.c_str());

	DBG_UDEV_INTEGER(bcdDevice);

	auto vp = usbip_names_get_product(ids, udev->idVendor, udev->idProduct);
	dbg("%-20s = %s", "Vendor/Product", vp.c_str());

	DBG_UDEV_INTEGER(bNumConfigurations);
	DBG_UDEV_INTEGER(bNumInterfaces);

	dbg("%-20s = %s", "speed", usbip_speed_string(static_cast<usb_device_speed>(udev->speed)));

	DBG_UDEV_INTEGER(busnum);
	DBG_UDEV_INTEGER(devnum);
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
