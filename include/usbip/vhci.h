/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
#include "offsetof_ex.h"

/*
 * Strings encoding is UTF8. 
 */

namespace usbip
{

/**
 * Check that the string consists only of ASCII alphanumeric characters.
 * @param s null-ternimated ASCII string
 * @param maxlen max buffer size
 * @return < 0 if parameters/characters are invalid,
 *         otherwise the same as strnlen(s, maxlen)
 */
constexpr SSIZE_T is_ascii_alnum(_In_opt_ const char *s, SSIZE_T maxlen);

constexpr auto is_ascii(unsigned char ch) { return ch < 0x80; }

} // namespace usbip


namespace usbip::vhci
{

DEFINE_GUID(GUID_DEVINTERFACE_USBIP_VHCI,
        0xB4030C06, 0xDC5F, 0x4FCC, 0x87, 0xEB, 0xE5, 0x51, 0x5A, 0x09, 0x35, 0xC0);

#ifndef USBIP_VHCI_ISOLATION_DEFINED
#define USBIP_VHCI_ISOLATION_DEFINED
  enum class isolation : unsigned char { none, session, user }; // see libusbip/vhci.h
#endif

struct sid_data
{
        UCHAR data[68]; // SECURITY_MAX_SID_SIZE, ntifs.h
        ULONG length;
};

static_assert(sizeof(session::system) == sizeof(ULONG)); // session id type

struct device_owner
{
        ULONG session_id = session::invalid;
        sid_data sid{};

        constexpr auto has_session() const noexcept { return session_id != session::invalid; }
        constexpr bool has_user() const noexcept { return sid.length; }
        constexpr auto empty() const noexcept { return !(has_session() || has_user()); }
};

struct isolation_data
{
        sid_data owner_sid;
        ULONG session_id;
        isolation mode;
};

struct base
{
        ULONG size; // IN, self size
};

struct imported_device_location
{
        char busid[BUS_ID_SIZE];
        char service[32]; // NI_MAXSERV
        char host[1025];  // NI_MAXHOST in ws2def.h
};

struct imported_device_config
{
        imported_device_location location;
        char serial[SERIAL_BUFSZ];
        bool wsk_events;
        isolation iso_mode;
};

struct imported_device_hardware
{
        UINT32 devid;
//      static_assert(sizeof(devid) == sizeof(usbip_header_basic::devid));

        usb_device_speed speed;
        static_assert(sizeof(speed) == sizeof(int));

        UINT16 vendor;
        UINT16 product;

        UCHAR iserial; // USB_DEVICE_DESCRIPTOR.iSerialNumber
};

struct imported_device
{
        int port; // OUT, >= 1 or zero if an error
        ULONG location_hash; // OUT, hash(host,service,busid)

        imported_device_config config;
        imported_device_hardware hw;
};
static_assert(!offsetof(imported_device, port)); // must be the first member
static_assert(offsetof(imported_device, location_hash) == sizeof(imported_device::port));

enum class state { unplugged, connecting, connected, plugged, disconnected, unplugging };

/*
 * There can be multiple event sources for one device,
 * each of them emits events with a unique source_id.
 */
struct device_state : base
{
        state state;
        ULONG source_id;
        imported_device device;
};

constexpr auto pack_attach_flags(bool once, bool wsk_events, isolation iso = isolation::none)
{
        return  (static_cast<ULONG>(iso)  << 2) |
                (static_cast<ULONG>(once) << 1) |
                 static_cast<ULONG>(wsk_events);
}

/*
 * Unknown flags are ignored.
 */
constexpr void unpack_attach_flags(
        _Inout_ bool &once, _Inout_ bool &wsk_events, _Inout_ isolation &iso, ULONG flags)
{
        wsk_events = flags & 1;
        once = flags & 2;
        iso = static_cast<isolation>((flags >> 2) & 0x3);
}

constexpr void unpack_attach_flags(_Inout_ bool &once, _Inout_ bool &wsk_events, ULONG flags)
{
        isolation iso;
        unpack_attach_flags(once, wsk_events, iso, flags);
}

} // namespace usbip::vhci


namespace usbip::vhci::ioctl
{

enum class function { // 12 bit
        plugin_hardware = 0x800, // values of less than 0x800 are reserved for Microsoft
        plugout_hardware, 
        get_imported_devices,
        set_persistent,
        get_persistent,
        stop_attach_attempts,
        plugin_hardware_once,
        plugout_hardware_and_reattach,
        internal_get_port_isolation,
};

constexpr auto make(function id, ULONG access = FILE_READ_DATA | FILE_WRITE_DATA)
{
        return CTL_CODE(FILE_DEVICE_UNKNOWN, static_cast<int>(id), METHOD_BUFFERED, access);
}

enum {
        PLUGIN_HARDWARE = make(function::plugin_hardware),
        PLUGOUT_HARDWARE = make(function::plugout_hardware),
        GET_IMPORTED_DEVICES = make(function::get_imported_devices),
        SET_PERSISTENT = make(function::set_persistent),
        GET_PERSISTENT = make(function::get_persistent),
        STOP_ATTACH_ATTEMPTS = make(function::stop_attach_attempts),
        PLUGIN_HARDWARE_ONCE = make(function::plugin_hardware_once),
        PLUGOUT_HARDWARE_AND_REATTACH = make(function::plugout_hardware_and_reattach), // for internal use only
        INTERNAL_GET_PORT_ISOLATION = make(function::internal_get_port_isolation, FILE_ANY_ACCESS),
};

struct plugin_hardware : base
{
        int port; // OUT, >= 1 or zero if an error
        ULONG location_hash; // OUT, hash(host,service,busid)

        imported_device_config config;
};
static_assert(offsetof_ex(plugin_hardware, port) == sizeof(base));
static_assert(offsetof_ex(plugin_hardware, location_hash) == sizeof(base) + sizeof(plugin_hardware::port));

struct stop_attach_attempts : base
{
        int count; // OUT, number of canceled requests
        imported_device_location location;
};

enum { PORT_ALL_CLOSEONLY = -2, PORT_ALL };

struct plugout_hardware : base
{
        int port;
};

struct get_imported_devices : base
{
        imported_device devices[ANYSIZE_ARRAY];
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto get_imported_devices_size(_In_ ULONG n)
{
        return offsetof_ex(get_imported_devices, devices) + n*sizeof(*get_imported_devices::devices);
}

struct get_port_isolation : base
{
        int port; // IN/OUT, >= 1 or zero if an error
        isolation_data data; // OUT
};

} // namespace usbip::vhci::ioctl


/*
 * UTF-8 is designed so that all ASCII characters (0–127)
 * are represented by a single byte with the high bit set to 0.
 */
constexpr SSIZE_T usbip::is_ascii_alnum(_In_opt_ const char *s, SSIZE_T maxlen)
{
        if (!(s && maxlen >= 0)) {
                return -1;
        }

        for (SSIZE_T i = 0; i < maxlen; ++i) {

                unsigned char c = s[i];
                if (!c) {
                        return i;
                }

                auto alnum = (c <= 'z' && c >= 'a') ||
                             (c <= 'Z' && c >= 'A') ||
                             (c <= '9' && c >= '0');

                if (!alnum) {
                        return -1;
                }
        }

        return maxlen;
}

static_assert(usbip::is_ascii_alnum(nullptr, 1) < 0);
static_assert(usbip::is_ascii_alnum("", -1) < 0);
static_assert(usbip::is_ascii_alnum("", 0) == 0);
static_assert(usbip::is_ascii_alnum("", 1) == 0);
static_assert(usbip::is_ascii_alnum("1", 2) == 1);
static_assert(usbip::is_ascii_alnum("1@", 3) < 0);
static_assert(usbip::is_ascii_alnum("1", 1) == 1);
static_assert(usbip::is_ascii_alnum("1\0W", 4) == 1);
