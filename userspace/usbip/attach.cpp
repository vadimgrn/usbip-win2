/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"

#include <libusbip\vhci.h>
#include <usbip\vhci.h>

#include <spdlog\spdlog.h>

#include <system_error>

bool usbip::cmd_attach(void *p)
{
        auto dev = vhci::open();
        if (!dev) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        auto &args = *reinterpret_cast<attach_args*>(p);
        vhci::ioctl_plugin_hardware r;

        if (auto err = init(r, args.remote, global_args.tcp_port, args.busid)) {
                auto msg = std::generic_category().message(err);
                spdlog::error("#{} {}", err, msg);
                return false;
        }

        auto port = vhci::attach(dev.get(), r);
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
