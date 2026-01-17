/*
 * Copyright (c) 2021-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "strings.h"

#include <libusbip\vhci.h>
#include <libusbip\persistent.h>

#include <format>

#pragma warning(push)
#pragma warning(disable: 4389) // signed/unsigned mismatch in spdlog headers
#include <spdlog\spdlog.h>
#pragma warning(pop)

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
        auto &loc = d.location;
        auto msg = std::format(fmt, d.port, get_speed_str(d.speed),
                                product,
                                loc.hostname, loc.service, loc.busid,
                                bus, dev);

        printf(msg.c_str());
}

} // namespace


bool usbip::cmd_port(void *p)
{
        auto &args = *reinterpret_cast<port_args*>(p); 

        auto dev = vhci::open();
        if (!dev) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        bool success;

        auto devices = vhci::get_imported_devices(dev.get(), success);
        if (!success) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        spdlog::debug("{} imported usb device(s)", devices.size());

        std::vector<device_location> dl;
        if (args.stash) {
                dl.reserve(devices.size());
        }

        auto &ports = args.ports; 
        auto found = false;

        for (auto &d: devices) {
                assert(d.port);
                if (ports.empty() || ports.contains(d.port)) {
                        if (!found) {
                                found = true;
                                printf("Imported USB devices\n"
                                       "====================\n");
                        }
                        print(d);
                        if (args.stash) {
                                dl.push_back(std::move(d.location));
                        }
                }
        }

        success = found || ports.empty();

        if (args.stash && !vhci::set_persistent(dev.get(), dl)) {
                spdlog::error(GetLastErrorMsg());
                success = false;
        }

        return success;
}
