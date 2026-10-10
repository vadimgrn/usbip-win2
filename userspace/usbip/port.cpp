/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "strings.h"
#include "log.h"

#include <libusbip/vhci.h>
#include <libusbip/persistent.h>

#include <print>

namespace
{

using namespace usbip;

void print(_In_ const imported_device &d)
{
        auto product = get_product(get_ids(), d.vendor, d.product);

        auto bus = static_cast<uint16_t>(d.devid >> 16);
        auto dev = static_cast<uint16_t>(d.devid & 0xFFFF);

        constexpr auto &fmt = R"(Port {:02}: device in use at {}
         {}
           -> usbip://{}:{}/{}
           -> remote bus/dev: {:03}/{:03}
           -> serial: {}
           -> mode: {}
           -> isolation: {})";

        auto &loc = d.config.location;

        std::println(fmt, d.port, get_speed_str(d.speed),
                        product,
                        loc.hostname, loc.service, loc.busid,
                        bus, dev,
                        d.config.serial,
                        to_string(d.config.recv_mode),
                        to_string(d.config.iso_mode));
}

} // namespace


bool usbip::cmd_port(const port_args &args)
{
        auto dev = vhci::open();
        if (!dev) {
                log::error(get_last_error_msg());
                return false;
        }

        auto devices = vhci::get_imported_devices(dev.get());
        if (!devices) {
                log::error(get_last_error_msg());
                return false;
        }

        log::debug("{} imported usb device(s)", devices->size());

        std::optional<std::vector<device_config>> persistent;
        if (args.persistent) {
                persistent.emplace();
                persistent->reserve(devices->size());
        }

        const auto &ports = args.ports;
        auto found = false;

        for (const auto &d: *devices) {
                assert(d.port);
                if (ports.empty() || ports.contains(d.port)) {
                        if (!found) {
                                found = true;
                                std::println("Imported USB devices\n"
                                             "====================");
                        }
                        print(d);
                        if (persistent) {
                                persistent->push_back(d.config);
                        }
                }
        }

        if (!found && !ports.empty()) {
                log::error("requested port(s) not found");
                return false;
        }

        if (persistent) {
                if (!vhci::set_persistent(dev.get(), *persistent)) {
                        log::error(get_last_error_msg());
                        return false;
                }
                log::debug("{} persistent device(s) stashed", persistent->size());
        }

        return true;
}
