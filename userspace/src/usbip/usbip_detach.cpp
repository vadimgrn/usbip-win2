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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <windows.h>

#include "getopt.h"
#include "usbip_common.h"
#include "usbip_vhci.h"

static const char usbip_detach_usage_string[] =
	"usbip detach <args>\n"
	"    -p, --port=<port>    "
	" port the device is on\n";

void usbip_detach_usage()
{
	printf("usage: %s", usbip_detach_usage_string);
}

static int detach_port(const char* portstr)
{
        int port{};

        if (*portstr == '*' && portstr[1] == '\0') {
                port = -1;
        } else if (sscanf_s(portstr, "%d", &port) != 1) {
		err("invalid port: %s", portstr);
		return 1;
	}
	
        auto hdev = usbip_vhci_driver_open();
	if (!hdev) {
		err("vhci driver is not loaded");
		return 2;
	}

	auto ret = usbip_vhci_detach_device(hdev.get(), port);
	hdev.reset();

	if (!ret) {
                if (port < 0) {
                        printf("all ports are detached\n");
                } else {
                        printf("port %d is succesfully detached\n", port);
                }
		return 0;
	}

        switch (ret) {
	case ERR_INVARG:
		err("invalid port: %d", port);
		break;
	case ERR_NOTEXIST:
		err("non-existent port: %d", port);
		break;
	default:
		err("failed to detach");
		break;
	}
	
        return 3;
}

int usbip_detach(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "port", required_argument, nullptr, 'p' },
		{ nullptr, 0, nullptr, 0 }
	};

	for (;;) {
		int	opt = getopt_long(argc, argv, "p:", opts, nullptr);

		if (opt == -1)
			break;

		switch (opt) {
		case 'p':
			return detach_port(optarg);
		default:
			break;
		}
	}

	err("port is required");
	usbip_detach_usage();

	return 1;
}