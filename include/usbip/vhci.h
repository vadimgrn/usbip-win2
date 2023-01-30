#pragma once

#include <guiddef.h>

#ifdef _KERNEL_MODE
  #include <wdm.h>
#else
  #include <windows.h>
  #include <winioctl.h>
#endif

#include "ch9.h"
#include "consts.h"

namespace usbip::vhci
{

enum { 
        USB2_PORTS = 30,
        USB3_PORTS = USB2_PORTS,
        TOTAL_PORTS = USB2_PORTS + USB3_PORTS
};

constexpr auto is_valid_port(int port)
{
        return port > 0 && port <= TOTAL_PORTS;
}

inline auto get_port_range(_In_ usb_device_speed speed)
{
        struct{ int begin;  int end; } r;

        if (speed < USB_SPEED_SUPER) {
                r.begin = 0;
                r.end = USB2_PORTS;
        } else {
                r.begin = USB2_PORTS;
                r.end = TOTAL_PORTS;
        }

        return r;
}

DEFINE_GUID(GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
        0xB4030C06, 0xDC5F, 0x4FCC, 0x87, 0xEB, 0xE5, 0x51, 0x5A, 0x09, 0x35, 0xC0);

enum class function { // 12 bit
        plugin = 0x800, // values of less than 0x800 are reserved for Microsoft
        plugout, 
        get_imported_devices 
};

constexpr auto make_ioctl(function id)
{
        return CTL_CODE(FILE_DEVICE_UNKNOWN, static_cast<int>(id), METHOD_BUFFERED, 
                        FILE_READ_DATA | FILE_WRITE_DATA);
}

namespace ioctl 
{
        enum {
                plugin_hardware      = make_ioctl(function::plugin),
                plugout_hardware     = make_ioctl(function::plugout),
                get_imported_devices = make_ioctl(function::get_imported_devices)
        };
} // namespace ioctl

/*
 * Strings encoding is UTF8. 
 */
struct ioctl_plugin_hardware // IN/OUT
{
        struct {
                int port; // [1..TOTAL_PORTS] or zero if an error
                int error;
        } out; // must be the first member

        auto get_err() const { return out.error < 0 ? static_cast<err_t>(out.error) : ERR_NONE; }
        auto get_status() const { return out.error > 0 ? static_cast<op_status_t>(out.error) : ST_OK; }

        char busid[USBIP_BUS_ID_SIZE];
        char service[32]; // NI_MAXSERV
        char host[1025];  // NI_MAXHOST in ws2def.h
};

struct ioctl_plugout_hardware // IN
{
        int port; // [1..TOTAL_PORTS] or all ports if <= 0
};

struct imported_dev_data
{
        UINT32 devid;
//      static_assert(sizeof(devid) == sizeof(usbip_header_basic::devid));

        usb_device_speed speed;
        static_assert(sizeof(speed) == sizeof(int));

        UINT16 vendor;
        UINT16 product;
};

struct imported_device : ioctl_plugin_hardware, imported_dev_data {}; // OUT, ioctl::get_imported_devices

} // namespace usbip::vhci
