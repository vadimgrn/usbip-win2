/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "ctrl_c_guard.h"
#include "strings.h"
#include "log.h"

#include <libusbip/vhci.h>
#include <libusbip/persistent.h>
#include <libusbip/volume_isolation.h>

#include <print>

namespace
{

using namespace usbip;

bool attach_persistent_devices(_In_ HANDLE dev)
{
        auto v = vhci::get_persistent(dev);
        if (!v) {
                log::error(get_last_error_msg());
                return false;
        }

        auto ok = true;

        for (const auto &dc: *v) {
                std::println("{}", dc);

                if (!vhci::attach(dev, {dc})) {
                        log::error(get_last_error_msg());
                        ok = false;
                }
        }

        return ok;
}

bool stop_attach_attempts(_In_ HANDLE dev, _In_opt_ const device_location *loc)
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

void isolate_and_print_volumes(_In_ int port)
{
        auto vols = isolate_port_volumes(port);
        if (!vols) {
                return;
        }

        for (const auto &v: *vols) {
                if (!v.drive_letter) {
                        continue;
                }

                if (!v.dacl_error) {
                        std::println("storage volume {}: isolated to current session (DACL restricted)",
                                     static_cast<char>(v.drive_letter));
                } else {
                        std::println("storage volume {}: isolated to current session (DACL error: {})",
                                     static_cast<char>(v.drive_letter), get_last_error_msg(v.dacl_error));
                }
        }
}

bool attach_device(_In_ HANDLE dev, const attach_args &args)
{
        vhci::attach_args cmd_args {
                .config = {
                        .location = {
                                .hostname = args.remote,
                                .service = global_args.tcp_port,
                                .busid = args.busid,
                        },
                        .serial = args.serial,
                        .recv_mode = args.recv_mode,
                        .iso_mode = args.iso_mode,
                },
                .once = args.once,
        };

        auto port = vhci::attach(dev, cmd_args);
        if (!port) {
                log::error(get_last_error_msg());
                return false;
        }

        if (args.terse) {
                std::println("{}", port);
        } else {
                std::println("successfully attached to port {}", port);
        }

        if (args.iso_mode != isolation::none) {
                isolate_and_print_volumes(port);
        }

        return true;
}

} // namespace


bool usbip::cmd_attach(const attach_args &args)
{
        auto dev = vhci::open();
        if (!dev) {
                log::error(get_last_error_msg());
                return false;
        }

        ctrl_c_guard guard(dev.get());

        if (args.persistent) {
                return attach_persistent_devices(dev.get());
        }

        if (args.stop_all) {
                return stop_attach_attempts(dev.get(), nullptr);
        }

        if (args.stop) {
                device_location loc {
                        .hostname = args.remote,
                        .service = global_args.tcp_port,
                        .busid = args.busid,
                };
                return stop_attach_attempts(dev.get(), &loc);
        }

        return attach_device(dev.get(), args);
}
