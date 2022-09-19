/*
 * Copyright (C) 2022 Vadym Hrynchyshyn
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
 */

#include "vhci.h"
#include "usbip.h"

#include <libusbip\common.h>
#include <libusbip\getopt.h>

#include <set>
#include <sstream>
#include <vector>

namespace
{

int usbip_vhci_imported_device_dump(const usbip::vhci::ioctl_imported_dev &d)
{
        if (d.status == VDEV_ST_NULL || d.status == VDEV_ST_NOTASSIGNED) {
                return 0;
        }

        printf("Port %02d: <%s> at %s\n", d.port, usbip_status_string(d.status), usbip_speed_string(d.speed));

        auto product_name = usbip_names_get_product(get_ids(), d.vendor, d.product);
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

auto get_imported_devices(std::vector<usbip::vhci::ioctl_imported_dev> &devs)
{
        auto hdev = usbip::vhci_driver_open();
        if (!hdev) {
                err("failed to open vhci driver");
                return 3;
        }
        
        auto v = usbip::vhci_get_imported_devs(hdev.get());
        if (v.empty()) {
                err("failed to get attach information");
                return 2;
        }
                
        for (auto &d: v) {
                if (d.port) {
                        devs.push_back(d);
                } else {
                        break;
                }
        }

        return 0;
}

int list_imported_devices(const std::set<int> &ports)
{
        std::vector<usbip::vhci::ioctl_imported_dev> devs;
        devs.reserve(usbip::vhci::TOTAL_PORTS);

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


void usbip_port_usage()
{
        const char fmt[] =
"usage: usbip port [portN...]\n"
"    portN      list given port(s) for checking, valid range is 1-%d\n";

        printf(fmt, usbip::vhci::TOTAL_PORTS);
}

int usbip_port_show(int argc, char *argv[])
{
        std::set<int> ports;

        for (int i = 1; i < argc; ++i) {

                auto str = argv[i];
                int port;

                if ((std::istringstream(str) >> port) && usbip::vhci::is_valid_vport(port)) {
                        ports.insert(port);
                } else {
                        err("invalid port: %s", str);
                        usbip_port_usage();
                        return 1;
                }
        }

	return list_imported_devices(ports);
}
