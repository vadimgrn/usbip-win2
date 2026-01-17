/*
 * Copyright (c) 2021-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include <libusbip\vhci.h>
#include <libusbip\persistent.h>

#pragma warning(push)
#pragma warning(disable: 4389) // signed/unsigned mismatch in spdlog headers
#include <spdlog\spdlog.h>
#pragma warning(pop)

namespace
{

using namespace usbip;

auto attach_stashed_devices(HANDLE dev)
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

} // namespace


bool usbip::cmd_attach(void *p)
{
        auto &args = *reinterpret_cast<attach_args*>(p);

        auto dev = vhci::open();
        if (!dev) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        if (args.stashed) {
                return attach_stashed_devices(dev.get());
        }

        device_location location {
                .hostname = args.remote, 
                .service = global_args.tcp_port, 
                .busid = args.busid,
        };

        auto port = vhci::attach(dev.get(), location);
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
