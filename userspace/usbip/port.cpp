/*
 * Copyright (C) 2021-2023 Vadym Hrynchyshyn
 *               2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "usbip.h"
#include "strings.h"

#include <libusbip\vhci.h>

#include <spdlog\spdlog.h>

namespace
{

using namespace usbip;

void dump(const vhci::ioctl_get_imported_devices &d)
{
        auto prod = get_product(get_ids(), d.vendor, d.product);
        USHORT bus = d.devid >> 16;
        USHORT dev = d.devid & 0xFFFF;

        printf( "Port %02d: device in use at %s\n"
                "        %s\n"
                "%10s -> usbip://%s:%s/%s\n"
                "%10s -> remote bus/dev %03d/%03d\n",
                d.port, get_speed_str(d.speed),
                prod.c_str(),
                " ", d.host, d.service, d.busid,
                " ", bus, dev);
}

auto get_imported_devices(std::vector<vhci::ioctl_get_imported_devices> &devs)
{
        auto dev = vhci::open();
        if (!dev) {
                spdlog::error("failed to open vhci device");
                return EXIT_FAILURE;
        }

        bool ok = false;
        devs = vhci::get_imported_devs(dev.get(), ok);
        if (!ok) {
                spdlog::error("failed to get imported devices information");
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}

} // namespace


int usbip::cmd_port(port_args &r)
{
        std::vector<vhci::ioctl_get_imported_devices> devs;
        if (auto err = get_imported_devices(devs)) {
                return err;
        }

        bool found = false;

        for (auto& d: devs) {
                assert(d.port);
                if (r.ports.empty() || r.ports.contains(d.port)) {
                        if (!found) {
                                found = true;
                                printf("Imported USB devices\n"
                                       "====================\n");
                        }
                        dump(d);
                }
        }

        if (!(found || r.ports.empty())) {
                return EXIT_FAILURE; // port check failed
        }

        return EXIT_SUCCESS;
}
