/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "ctrl_c_guard.h"
#include "log.h"
#include <libusbip/vhci.h>

#include <print>

bool usbip::cmd_detach(const detach_args &args)
{
        auto dev = vhci::open();
        if (!dev) {
                log::error(get_last_error_msg());
                return false;
        }

        ctrl_c_guard guard(dev.get());
        auto ok = vhci::detach(dev.get(), args.port);

        if (!ok) {
                log::error(get_last_error_msg());
        } else if (args.port == vhci::port_all_closeonly) {
                std::println("all network connections are closed");
        } else if (args.port <= 0) {
                std::println("all ports are detached");
        } else {
                std::println("port {} is successfully detached", args.port);
        }

        return ok;
}
