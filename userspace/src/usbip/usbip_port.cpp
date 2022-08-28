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

int list_imported_devices(int port)
{
        auto hdev = usbip_vhci_driver_open(usbip_hci::usb2);
        if (!hdev) {
                err("failed to open vhci driver");
                return 3;
        }

        auto idevs = usbip_vhci_get_imported_devs(hdev.get());
        if (idevs.empty()) {
                err("failed to get attach information");
                return 2;
        }

        printf("Imported USB devices\n");
        printf("====================\n");

        bool found{};

        for (auto& d : idevs) {
                if (!d.port) {
                        break;
                }
                if (port > 0) {
                        if (port != d.port) {
                                continue;
                        }
                        found = true;
                }
                usbip_vhci_imported_device_dump(d);
        }

        if (port > 0 && !found) {
                /* port check failed */
                return 2;
        }

        return 0;
}

} // namespace


void usbip_port_usage()
{
        const char msg[] =
"usage: usbip port <args>\n"
"    -p, --port=<port>      list only given port(for port checking)\n";

        printf(msg);
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

	return list_imported_devices(port);
}
