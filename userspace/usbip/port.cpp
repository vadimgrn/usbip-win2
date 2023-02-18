/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "strings.h"

#include <libusbip\vhci.h>

#include <format>
#include <spdlog\spdlog.h>

namespace
{

using namespace usbip;

void print(const imported_device &d)
{
        auto product = get_product(get_ids(), d.vendor, d.product);

        USHORT bus = d.devid >> 16;
        USHORT dev = d.devid & 0xFFFF;

        constexpr auto &fmt = R"(Port {:02}: device in use at {}
         {}
           -> usbip://{}:{}/{}
           -> remote bus/dev {:03}/{:03}
)";
        auto msg = std::format(fmt, d.hub_port, get_speed_str(d.speed),
                                product,
                                d.hostname, d.service, d.busid,
                                bus, dev);

        printf(msg.c_str());
}

} // namespace


bool usbip::cmd_port(void *p)
{
        auto dev = vhci::open();
        if (!dev) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        bool ok;
        auto devices = vhci::get_imported_devices(dev.get(), ok);
        if (!ok) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        spdlog::debug("{} imported usb device(s)", devices.size());

        auto &ports = reinterpret_cast<port_args*>(p)->ports; 
        bool found{};

        for (auto &d: devices) {
                assert(d.hub_port);
                if (ports.empty() || ports.contains(d.hub_port)) {
                        if (!found) {
                                found = true;
                                printf("Imported USB devices\n"
                                       "====================\n");
                        }
                        print(d);
                }
        }

        return found || ports.empty();
}
