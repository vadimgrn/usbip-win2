/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include <libusbip\vhci.h>
#include <libusbip\persistent.h>

#include <spdlog\spdlog.h>

namespace
{

using namespace usbip;

auto attach_persistent_devices(HANDLE dev)
{
        if (auto v = vhci::get_persistent(dev); !v) {
                spdlog::error(GetLastErrorMsg());
                return false;
        } else for (auto &i: *v) {
                printf("%s:%s/%s\n", i.hostname.c_str(), i.service.c_str(), i.busid.c_str());
                if (vhci::attach_args args{ .location = std::move(i) }; !vhci::attach(dev, args)) {
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
                .once = args.once
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
                printf("%d\n", port);
        } else {
                printf("succesfully attached to port %d\n", port);
        }

        return true;
}
