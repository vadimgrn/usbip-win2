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

#include <usbip\proto_op.h>

#include <libusbip\network.h>
#include <libusbip\remote.h>

#include <spdlog\spdlog.h>

namespace
{

using namespace usbip;

void on_device_count(int count)
{
	if (count) {
		printf("Exportable USB devices\n"
			"======================\n");
	}
}

void on_device(int, const usbip_usb_device &d)
{
	auto &ids = get_ids();
	auto prod = get_product(ids, d.idVendor, d.idProduct);
	auto csp = get_class(ids, d.bDeviceClass, d.bDeviceSubClass, d.bDeviceProtocol);

	printf( "%11s: %s\n"
		"%11s: %s\n"
		"%11s: %s\n",
		d.busid, prod.c_str(),
		"", d.path,
		"", csp.c_str());

	if (!d.bNumInterfaces) {
		printf("\n");
	}
}

void on_interface(int, const usbip_usb_device &d, int idx, const usbip_usb_interface &r)
{
	auto &ids = get_ids();

	auto csp = get_class(ids, r.bInterfaceClass, r.bInterfaceSubClass, r.bInterfaceProtocol);
	printf("%11s: %2d - %s\n", "", idx, csp.c_str());

	if (idx + 1 == d.bNumInterfaces) { // last
		printf("\n");
	}
}

} // namespace


int usbip::cmd_list(void *p)
{
	auto &r = *reinterpret_cast<list_args*>(p);

	auto sock = net::tcp_connect(r.remote.c_str(), global_args.tcp_port.c_str());
	if (!sock) {
		spdlog::error("can't connect to {}:{}", r.remote, global_args.tcp_port);
		return EXIT_FAILURE;
	}

	spdlog::debug("connected to {}:{}", r.remote, global_args.tcp_port);

	if (!enum_exportable_devices(sock.get(), on_device, on_interface, on_device_count)) {
		spdlog::error("failed to get exportable device list");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
