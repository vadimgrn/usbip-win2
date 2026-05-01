/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "strings.h"

#include <libusbip\vhci.h>
#include <libusbip\persistent.h>

#include <spdlog\spdlog.h>
#include <print>

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
           -> serial '{}')";

        auto &loc = d.location;

        std::println(fmt, d.port, get_speed_str(d.speed),
                          product,
                          loc.hostname, loc.service, loc.busid,
                          bus, dev,
                          d.serial);
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

        auto devices = vhci::get_imported_devices(dev.get());
        if (!devices) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        spdlog::debug("{} imported usb device(s)", devices->size());

        std::optional<std::vector<persistent_device>> persistent;
        if (args.persistent) {
                persistent.emplace();
                persistent->reserve(devices->size());
        }

        auto &ports = args.ports; 
        auto found = false;

        for (auto &d: *devices) {
                assert(d.port);
                if (ports.empty() || ports.contains(d.port)) {
                        if (!found) {
                                found = true;
                                std::println("Imported USB devices\n"
                                             "====================");
                        }
                        print(d);
                        if (persistent) {
                                persistent_device pd {
                                        .location = std::move(d.location),
                                        .serial = std::move(d.serial),
                                };
                                persistent->push_back(std::move(pd));
                        }
                }
        }

        auto ok = found || ports.empty();

        if (persistent && !vhci::set_persistent(dev.get(), *persistent)) {
                spdlog::error(GetLastErrorMsg());
                ok = false;
        }

        return ok;
}
