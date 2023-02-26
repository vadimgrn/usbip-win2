/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include <libusbip\vhci.h>

#include <spdlog\spdlog.h>

bool usbip::cmd_attach(void *p)
{
        auto dev = vhci::open();
        if (!dev) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        auto &args = *reinterpret_cast<attach_args*>(p);

        attach_info info {
                .hostname = args.remote, 
                .service = global_args.tcp_port, 
                .busid = args.busid,
        };

        auto port = vhci::attach(dev.get(), info);
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
