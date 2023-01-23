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
using namespace libusbip;

auto get_exported_devices(const std::string &host, SOCKET sockfd)
{
	if (!net::send_op_common(sockfd, OP_REQ_DEVLIST, 0)) {
		spdlog::error("failed to send common header");
		return false;
	}

	uint16_t code = OP_REP_DEVLIST;
	int status = 0;

	if (auto err = net::recv_op_common(sockfd, &code, &status)) {
		spdlog::error("failed to recv common header: {}", dbg_errcode(err));
		return false;
	}

	op_devlist_reply reply;
	
	if (net::recv(sockfd, &reply, sizeof(reply))) {
		PACK_OP_DEVLIST_REPLY(0, &reply);
	} else {
		spdlog::error("failed to recv devlist");
		return false;
	}

	if (!reply.ndev) {
		spdlog::info("no exportable devices found on '{}'", host);
		return true;
	}

	printf("Exportable USB devices\n");
	printf("======================\n");
	printf(" - %s\n", host.c_str());

	for (UINT32 i = 0; i < reply.ndev; ++i) {

		usbip_usb_device udev;

		if (net::recv(sockfd, &udev, sizeof(udev))) {
			usbip_net_pack_usb_device(0, &udev);
		} else {
			spdlog::error("failed to recv devlist: usbip_usb_device[{}]", i);
			return false;
		}

		auto &ids = get_ids();

		auto product_name = get_product(ids, udev.idVendor, udev.idProduct);
		auto class_name = get_class(ids, udev.bDeviceClass, udev.bDeviceSubClass, udev.bDeviceProtocol);

		printf("%11s: %s\n", udev.busid, product_name.c_str());
		printf("%11s: %s\n", "", udev.path);
		printf("%11s: %s\n", "", class_name.c_str());

		for (int j = 0; j < udev.bNumInterfaces; ++j) {
			usbip_usb_interface uintf;

			if (net::recv(sockfd, &uintf, sizeof(uintf))) {
				usbip_net_pack_usb_interface(0, &uintf);
			} else {
				spdlog::error("failed to recv devlist: usbip_usb_intf[{}]", j);
				return false;
			}

			auto csp = get_class(ids, uintf.bInterfaceClass, uintf.bInterfaceSubClass, uintf.bInterfaceProtocol);
			printf("%11s: %2d - %s\n", "", j, csp.c_str());
		}

		printf("\n");
	}

	return true;
}

} // namespace


int usbip::cmd_list(list_args &r)
{
	auto port = std::to_string(global_args.tcp_port);

	auto sock = net::tcp_connect(r.remote.c_str(), port.c_str());
	if (!sock) {
		spdlog::error("failed to connect a remote host '{}'", r.remote);
		return 3;
	}

	spdlog::debug("connected to {}:{}\n", r.remote, port);

	if (!get_exported_devices(r.remote, sock.get())) {
		spdlog::error("failed to get device list from '{}'", r.remote);
		return 4;
	}

	return 0;
}
