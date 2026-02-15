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
        bool success;
        
        if (auto v = vhci::get_persistent(dev, success); !success) {
                spdlog::error(GetLastErrorMsg());
        } else for (auto &i: v) {
                printf("%s:%s/%s\n", i.hostname.c_str(), i.service.c_str(), i.busid.c_str());
                if (!vhci::attach(dev, i)) {
                        spdlog::error(GetLastErrorMsg());
                }
        }

        return success;
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

        device_location location {
                .hostname = args.remote, 
                .service = global_args.tcp_port, 
                .busid = args.busid,
        };

        if (args.stop || args.stop_all) {
                assert(args.stop != args.stop_all);
                return stop_attach_attempts(dev.get(), args.stop ? &location : nullptr);
        }

        auto port = vhci::attach(dev.get(), location, args.once);
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
