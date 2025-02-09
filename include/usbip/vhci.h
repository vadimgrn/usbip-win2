/*
 * Copyright (C) 2021 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <cstddef>
#include <guiddef.h>

#ifdef _KERNEL_MODE
  #include <wdm.h>
  #include <minwindef.h>
#else
  #include <windows.h>
  #include <winioctl.h>
#endif

#include "ch9.h"
#include "consts.h"

/*
 * Strings encoding is UTF8. 
 */

namespace usbip::vhci
{

DEFINE_GUID(GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
        0xB4030C06, 0xDC5F, 0x4FCC, 0x87, 0xEB, 0xE5, 0x51, 0x5A, 0x09, 0x35, 0xC0);

struct base
{
        ULONG size; // IN, self size
};

struct imported_device_location
{
        int port; // OUT, >= 1 or zero if an error

        char busid[BUS_ID_SIZE];
        char service[32]; // NI_MAXSERV
        char host[1025];  // NI_MAXHOST in ws2def.h
};
static_assert(!offsetof(imported_device_location, port)); // must be the first member

struct imported_device_properties
{
        UINT32 devid;
//      static_assert(sizeof(devid) == sizeof(usbip_header_basic::devid));

        usb_device_speed speed;
        static_assert(sizeof(speed) == sizeof(int));

        UINT16 vendor;
        UINT16 product;
};

struct imported_device : imported_device_location, imported_device_properties {};

enum class state { unplugged, connecting, connected, plugged, disconnected, unplugging };

struct device_state : base, imported_device
{
        state state;
};

} // namespace usbip::vhci


namespace usbip::vhci::ioctl
{

enum class function { // 12 bit
        plugin_hardware = 0x800, // values of less than 0x800 are reserved for Microsoft
        plugout_hardware, 
        get_imported_devices,
        set_persistent,
        get_persistent,
};

constexpr auto make(function id)
{
        return CTL_CODE(FILE_DEVICE_UNKNOWN, static_cast<int>(id), METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA);
}

enum {
        PLUGIN_HARDWARE = make(function::plugin_hardware),
        PLUGOUT_HARDWARE = make(function::plugout_hardware),
        GET_IMPORTED_DEVICES = make(function::get_imported_devices),
        SET_PERSISTENT = make(function::set_persistent),
        GET_PERSISTENT = make(function::get_persistent),
};

struct plugin_hardware : base, imported_device_location {};

struct plugout_hardware : base
{
        int port; // all ports if <= 0
};

struct get_imported_devices : base
{
        imported_device devices[ANYSIZE_ARRAY];
};

constexpr auto get_imported_devices_size(_In_ ULONG n)
{
        return offsetof(get_imported_devices, devices) + n*sizeof(*get_imported_devices::devices);
}

} // namespace usbip::vhci::ioctl
