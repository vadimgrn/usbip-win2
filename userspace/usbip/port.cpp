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

        if (args.list_saved) {
                if (auto v = vhci::get_saved_devices(dev.get(), success); !success) {
                        spdlog::error(GetLastErrorMsg());
                } else for (auto &i: v) {
                        printf("%s:%s/%s\n", i.hostname.c_str(), i.service.c_str(), i.busid.c_str());
                }
                return success;
        }

        auto devices = vhci::get_imported_devices(dev.get(), success);
        if (!success) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        spdlog::debug("{} imported usb device(s)", devices.size());

        std::vector<device_location> dl;
        if (args.save) {
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
                        if (args.save) {
                                dl.push_back(std::move(d.location));
                        }
                }
        }

        success = found || ports.empty();

        if (args.save && !vhci::save_devices(dev.get(), dl)) {
                spdlog::error(GetLastErrorMsg());
                success = false;
        }

        return success;
}
