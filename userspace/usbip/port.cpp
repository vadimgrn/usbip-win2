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

int usbip_vhci_imported_device_dump(const vhci::ioctl_imported_dev &d)
{
        printf("Port %02d: device in use at %s\n", d.port, get_speed_str(d.speed));

        auto product_name = get_product(get_ids(), d.vendor, d.product);
        printf("       %s\n", product_name.c_str());

        printf("%10s -> usbip://%s:%s/%s\n", " ", d.host, d.service, d.busid);

        USHORT bus = d.devid >> 16;
        USHORT dev = d.devid & 0xFFFF;
        printf("%10s -> remote bus/dev %03d/%03d\n", " ", bus, dev);

        if (*d.serial) {
                printf("%10s -> serial '%s'\n", " ", d.serial);
        }

        return 0;
}

auto get_imported_devices(std::vector<vhci::ioctl_imported_dev> &devs)
{
        auto dev = vhci::open();
        if (!dev) {
                spdlog::error("failed to open vhci device");
                return 3;
        }

        bool ok = false;
        devs = vhci::get_imported_devs(dev.get(), ok);
        if (!ok) {
                spdlog::error("failed to get imported devices information");
                return 2;
        }

        return 0;
}

int list_imported_devices(const std::set<int> &ports)
{
        std::vector<vhci::ioctl_imported_dev> devs;
        if (auto err = get_imported_devices(devs)) {
                return err;
        }

        printf("Imported USB devices\n");
        printf("====================\n");

        bool found = false;

        for (auto& d: devs) {
                assert(d.port);
                if (ports.empty() || ports.contains(d.port)) {
                        usbip_vhci_imported_device_dump(d);
                        found = true;
                }

        }

        if (!(found || ports.empty())) {
                return 2; // port check failed
        }

        return 0;
}

} // namespace


int usbip::cmd_port(port_args &r)
{
        std::vector<vhci::ioctl_imported_dev> devs;
        if (auto err = get_imported_devices(devs)) {
                return err;
        }

        printf("Imported USB devices\n");
        printf("====================\n");

        bool found = false;

        for (auto& d: devs) {
                assert(d.port);
                if (r.ports.empty() || r.ports.contains(d.port)) {
                        usbip_vhci_imported_device_dump(d);
                        found = true;
                }

        }

        if (!(found || r.ports.empty())) {
                return 2; // port check failed
        }

        return 0;
}