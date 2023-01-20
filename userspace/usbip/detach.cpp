/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *               2022-2023 Vadym Hrynchyshyn
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

#include <libusbip\vhci.h>
#include <libusbip\getopt.h>

#include <spdlog\spdlog.h>

namespace
{

using namespace usbip;

int detach_port(const char *portstr)
{
	int port{};

	if (!strcmp(portstr, "*")) {
		port = -1;
	} else if (sscanf_s(portstr, "%d", &port) != 1) {
		spdlog::error("invalid port: {}", portstr);
		return 1;
	}

	if (port > vhci::TOTAL_PORTS) {
		spdlog::error("invalid port {}, max is {}", port, vhci::TOTAL_PORTS);
		return 1;
	}

	auto hdev = vhci::open();
	if (!hdev) {
		spdlog::error("can't open vhci device");
		return 2;
	}

	auto ret = vhci::detach_device(hdev.get(), port);
	hdev.reset();

	if (!ret) {
		if (port <= 0) {
			printf("all ports are detached\n");
		} else {
			printf("port %d is succesfully detached\n", port);
		}
		return 0;
	}

	switch (ret) {
	case ERR_INVARG:
		spdlog::error("invalid port: {}", port);
		break;
	case ERR_NOTEXIST:
		spdlog::error("non-existent port: {}", port);
		break;
	default:
		spdlog::error("failed to detach");
	}

	return 3;
}

} // namespace


void usbip_detach_usage()
{
	auto &fmt = 
"usage: usbip detach <args>\n"
"    -p, --port=<port>    "
" port the device is on, max %d, * or below 1 - all ports\n";

	printf(fmt, vhci::TOTAL_PORTS);
}

int usbip_detach(int argc, char *argv[])
{
	const option opts[] = 
        {
		{ "port", required_argument, nullptr, 'p' },
		{}
	};

	for (;;) {
		auto opt = getopt_long(argc, argv, "p:", opts, nullptr);

		if (opt == -1)
			break;

		switch (opt) {
		case 'p':
			return detach_port(optarg);
		}
	}

	spdlog::error("port is required");
	usbip_detach_usage();

	return 1;
}