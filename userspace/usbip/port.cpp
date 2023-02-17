/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "strings.h"

#include <libusbip\vhci.h>
#include <usbip\vhci.h>

#include <spdlog\spdlog.h>

namespace
{

using namespace usbip;

void print(const vhci::imported_device &d)
{
        auto prod = get_product(get_ids(), d.vendor, d.product);

        USHORT bus = d.devid >> 16;
        USHORT dev = d.devid & 0xFFFF;

        printf( "Port %02d: device in use at %s\n"
                "         %s\n"
                "%10s -> usbip://%s:%s/%s\n"
                "%10s -> remote bus/dev %03d/%03d\n",
                d.out.port, get_speed_str(d.speed),
                prod.c_str(),
                " ", d.host, d.service, d.busid,
                " ", bus, dev);
}

} // namespace


bool usbip::cmd_port(void *p)
{
        auto dev = vhci::open();
        if (!dev) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        bool ok;
        auto devices = vhci::get_imported_devices(dev.get(), ok);
        if (!ok) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        spdlog::debug("{} imported usb device(s)", devices.size());

        auto &ports = reinterpret_cast<port_args*>(p)->ports; 
        bool found{};

        for (auto &d: devices) {
                assert(d.out.port);
                if (ports.empty() || ports.contains(d.out.port)) {
                        if (!found) {
                                found = true;
                                printf("Imported USB devices\n"
                                       "====================\n");
                        }
                        print(d);
                }
        }

        return found || ports.empty();
}
