/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "ctrl_c_guard.h"
#include "strings.h"
#include "log.h"

#include <libusbip/vhci.h>
#include <libusbip/persistent.h>

#include <print>

namespace
{

using namespace usbip;

void print_exportable_devices(const std::vector<usb_device> &devices)
{
        if (devices.empty()) {
                return;
        }

        std::println("Exportable USB devices\n"
                     "======================");

        auto &ids = get_ids();

        for (const auto &d: devices) {
                auto prod = get_product(ids, d.idVendor, d.idProduct);
                auto csp = get_class(ids, d.bDeviceClass, d.bDeviceSubClass, d.bDeviceProtocol);

                auto lines = std::format(
                                "{:^11}: {}\n"
                                "{:11}: {}\n"
                                "{:11}: {}\n",
                                d.busid, prod,
                                "", d.path,
                                "", csp);

                if (d.interfaces.empty()) {
                        lines += '\n';
                }

                std::print("{}", lines);

                for (size_t idx = 0; idx < d.interfaces.size(); ++idx) {
                        const auto &r = d.interfaces[idx];
                        auto intf_csp = get_class(ids, r.bInterfaceClass, r.bInterfaceSubClass, r.bInterfaceProtocol);

                        auto s = std::format("{:11}: {:2} - {}\n", "", idx, intf_csp);
                        if (idx + 1 == d.interfaces.size()) {
                                s += '\n';
                        }

                        std::print("{}", s);
                }
        }
}

bool list_persistent_devices()
{
        auto dev = vhci::open();
        if (!dev) {
                log::error(get_last_error_msg());
                return false;
        }

        auto v = vhci::get_persistent(dev.get());
        if (!v) {
                log::error(get_last_error_msg());
                return false;
        }

        for (const auto &i: *v) {
                std::println("{}", i);
        }

        return true;
}

bool list_exportable_devices(const list_args &args)
{
        ctrl_c_guard guard;
        auto sock = connect(args.remote, global_args.tcp_port, guard.token());
        if (!sock) {
                log::error(get_last_error_msg());
                return false;
        }

        std::stop_callback stop_cb(guard.token(), [s = sock.get()] { cancel_io(s); });

        if (auto devs = get_exportable_devices(sock.get())) {
                print_exportable_devices(*devs);
                return true;
        }

        if (guard.token().stop_requested()) {
                SetLastError(ERROR_CANCELLED);
        }

        log::error(get_last_error_msg());
        return false;
}

} // namespace


bool usbip::cmd_list(const list_args &args)
{
        if (args.persistent) {
                return list_persistent_devices();
        }

        return list_exportable_devices(args);
}
