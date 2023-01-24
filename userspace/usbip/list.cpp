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
#include <libusbip\vhci.h>
#include <libusbip\dbgcode.h>

#include <spdlog\spdlog.h>

namespace
{

using namespace usbip;

auto get_exported_devices(SOCKET s)
{
	const auto cmd = OP_REQ_DEVLIST;

	if (!net::send_op_common(s, cmd)) {
		spdlog::error("send_op_common");
		return false;
	}

	if (auto err = net::recv_op_common(s, cmd)) {
		spdlog::error("recv_op_common {}", errt_str(err));
		return false;
	}

	op_devlist_reply reply;
	
	if (net::recv(s, &reply, sizeof(reply))) {
		PACK_OP_DEVLIST_REPLY(false, &reply);
	} else {
		spdlog::error("recv op_devlist_reply");
		return false;
	}

	if (!reply.ndev) {
		spdlog::info("no exportable devices found");
		return true;
	}

	printf("Exportable USB devices\n"
	       "======================\n");

	for (UINT32 i = 0; i < reply.ndev; ++i) {

		usbip_usb_device dev;

		if (net::recv(s, &dev, sizeof(dev))) {
			usbip_net_pack_usb_device(false, &dev);
		} else {
			spdlog::error("recv usbip_usb_device[{}]", i);
			return false;
		}

		auto &ids = get_ids();

		auto product_name = get_product(ids, dev.idVendor, dev.idProduct);
		auto class_name = get_class(ids, dev.bDeviceClass, dev.bDeviceSubClass, dev.bDeviceProtocol);

		printf("%11s: %s\n"
		       "%11s: %s\n"
		       "%11s: %s\n",
			dev.busid, product_name.c_str(),
			"", dev.path,
			"", class_name.c_str());

		for (int j = 0; j < dev.bNumInterfaces; ++j) {

			usbip_usb_interface intf;

			if (net::recv(s, &intf, sizeof(intf))) {
				usbip_net_pack_usb_interface(false, &intf);
			} else {
				spdlog::error("recv usbip_usb_intf[{}]", j);
				return false;
			}

			auto csp = get_class(ids, intf.bInterfaceClass, intf.bInterfaceSubClass, intf.bInterfaceProtocol);
			printf("%11s: %2d - %s\n", "", j, csp.c_str());
		}

		printf("\n");
	}

	return true;
}

} // namespace


int usbip::cmd_list(list_args &r)
{
	auto sock = net::tcp_connect(r.remote.c_str(), global_args.tcp_port.c_str());
	if (!sock) {
		spdlog::error("can't connect to {}:{}", r.remote, global_args.tcp_port);
		return 3;
	}

	spdlog::debug("connected to {}:{}", r.remote, global_args.tcp_port);

	if (!get_exported_devices(sock.get())) {
		spdlog::error("failed to get exported device list");
		return 4;
	}

	return 0;
}
