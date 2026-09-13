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

auto attach_persistent_devices(HANDLE dev)
{
        auto v = vhci::get_persistent(dev);
        if (!v) {
                log::error(get_last_error_msg());
                return false;
        }

        auto ok = true;
        for (const auto &args: *v) {
                std::println("{}", args);

                if (!vhci::attach(dev, args)) {
                        log::error(get_last_error_msg());
                        ok = false;
                }
        }

        return ok;
}

auto stop_attach_attempts(_In_ HANDLE dev, _In_opt_ const device_location *loc)
{
        auto cnt = vhci::stop_attach_attempts(dev, loc);
        auto ok = cnt >= 0;

        if (ok) {
                log::debug("{} request(s) stopped", cnt);
        } else {
                log::error(get_last_error_msg());
        }

        return ok;
}

} // namespace


bool usbip::cmd_attach(const attach_args &args)
{
        auto dev = vhci::open();
        if (!dev) {
                log::error(get_last_error_msg());
                return false;
        }

        if (args.persistent) {
                return attach_persistent_devices(dev.get());
        }

        if (args.stop_all) {
                return stop_attach_attempts(dev.get(), nullptr);
        }

        device_location loc {
                .hostname = args.remote,
                .service = global_args.tcp_port,
                .busid = args.busid,
        };

        if (args.stop) {
                return stop_attach_attempts(dev.get(), &loc);
        }

        vhci::attach_args cmd_args {
                .location = std::move(loc),
                .serial = args.serial,
                .recv_mode = args.recv_mode,
                .once = args.once,
        };

        auto port = vhci::attach(dev.get(), cmd_args);
        if (!port) {
                log::error(get_last_error_msg());
                return false;
        }

        if (args.terse) {
                std::println("{}", port);
        } else {
                std::println("successfully attached to port {}", port);
        }

        return true;
}
