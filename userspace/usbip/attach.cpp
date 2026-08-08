/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"

#include <libusbip/vhci.h>
#include <libusbip/persistent.h>

#include <spdlog/spdlog.h>
#include <print>

namespace
{

using namespace usbip;

auto attach_persistent_devices(HANDLE dev)
{
        if (auto v = vhci::get_persistent(dev); !v) {
                spdlog::error(GetLastErrorMsg());
                return false;
        } else for (auto &args: *v) {
                auto &loc = args.location;

                std::println("{}:{}/{}, serial '{}', mode:{}, once:{}", 
                              loc.hostname, loc.service, loc.busid, args.serial,
                              to_string(args.recv_mode), args.once);

                if (!vhci::attach(dev, args)) {
                        spdlog::error(GetLastErrorMsg());
                }
        }

        return true;
}

auto stop_attach_attempts(_In_ HANDLE dev, _In_opt_ const device_location *loc)
{
        auto cnt = vhci::stop_attach_attempts(dev, loc);
        auto ok = cnt >= 0;

        if (ok) {
                spdlog::debug("{} request(s) stopped", cnt);
        } else {
                spdlog::error(GetLastErrorMsg());
        }

        return ok;
}

} // namespace


bool usbip::cmd_attach(void *p)
{
        auto &args = *reinterpret_cast<attach_args*>(p);

        auto dev = vhci::open();
        if (!dev) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        if (args.persistent) {
                return attach_persistent_devices(dev.get());
        }

        vhci::attach_args cmd_args {
                .location {
                        .hostname = std::move(args.remote), 
                        .service = global_args.tcp_port, 
                        .busid = std::move(args.busid),
                },
                .serial = std::move(args.serial),
                .recv_mode = args.recv_mode,
                .once = args.once,
        };

        if (args.stop || args.stop_all) {
                assert(args.stop != args.stop_all);
                return stop_attach_attempts(dev.get(), args.stop ? &cmd_args.location : nullptr);
        }

        auto port = vhci::attach(dev.get(), cmd_args);
        if (!port) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        if (args.terse) {
                std::println("{}", port);
        } else {
                std::println("succesfully attached to port {}", port);
        }

        return true;
}
