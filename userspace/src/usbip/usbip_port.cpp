/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
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

#include <windows.h>

#include "usbip_common.h"
#include "usbip_vhci.h"
#include "getopt.h"
#include "usbip.h"

namespace
{

int usbip_vhci_imported_device_dump(const ioctl_usbip_vhci_imported_dev &d)
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

auto get_imported_devices(std::vector<ioctl_usbip_vhci_imported_dev> &devs, int port)
{
        vdev_usb_t versions[ARRAYSIZE(vdev_versions)];
        int cnt = ARRAYSIZE(vdev_versions);

        if (port > 0) {
                *versions = get_vdev_usb(port);
                cnt = 1; 
        } else {
                RtlCopyMemory(versions, vdev_versions, sizeof(versions));
        }

        for (int i = 0; i < cnt; ++i) {

                auto hdev = usbip::vhci_driver_open(versions[i]);
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
        }

        return 0;
}

int list_imported_devices(int port)
{
        std::vector<ioctl_usbip_vhci_imported_dev> devs;
        devs.reserve(USBIP_TOTAL_PORTS);

        if (auto err = get_imported_devices(devs, port)) {
                return err;
        }

        printf("Imported USB devices\n");
        printf("====================\n");

        bool found{};

        for (auto rhport = get_rhport(port); auto& d: devs) {

                assert(d.port);

                if (port > 0) {
                        if (rhport != d.port) {
                                continue;
                        }
                        found = true;
                }

                usbip_vhci_imported_device_dump(d);
        }

        if (port > 0 && !found) {
                return 2; // port check failed
        }

        return 0;
}

} // namespace


void usbip_port_usage()
{
        const char msg[] =
"usage: usbip port <args>\n"
"    -p, --port=<port>      list only given port (for port checking, [1..%d]), value below 1 means all ports\n";

        printf(msg, USBIP_TOTAL_PORTS);
}

int usbip_port_show(int argc, char *argv[])
{
	const option opts[] = {
		{ "port", required_argument, nullptr, 'p' },
		{}
	};

	int port = 0;

	while (true) {
		auto opt = getopt_long(argc, argv, "p:", opts, nullptr);
		if (opt == -1) {
			break;
                }

		switch (opt) {
		case 'p':
			if (sscanf_s(optarg, "%d", &port) != 1) {
				err("invalid port: %s", optarg);
				usbip_port_usage();
				return 1;
			}
			break;
		default:
			err("invalid option: %c", opt);
			usbip_port_usage();
			return 1;
		}
	}

        if (!(port > 0 && port <= USBIP_TOTAL_PORTS)) {
                err("invalid port %d, must be 1-%d", port, USBIP_TOTAL_PORTS);
                usbip_port_usage();
                return 1;
        }

	return list_imported_devices(port);
}
