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

#include "usbip.h"
#include "strings.h"

#include <libusbip\network.h>
#include <libusbip\dbgcode.h>

#include <usbip\proto_op.h>
#include <spdlog\spdlog.h>

namespace
{

using namespace usbip;

int get_exported_devices(const char *host, SOCKET sockfd)
{
	auto rc = usbip_net_send_op_common(sockfd, OP_REQ_DEVLIST, 0);
	if (rc < 0) {
		spdlog::error("failed to send common header: {}", dbg_errcode(rc));
		return ERR_NETWORK;
	}

	uint16_t code = OP_REP_DEVLIST;
	int status = 0;

	rc = usbip_net_recv_op_common(sockfd, &code, &status);
	if (rc < 0) {
		spdlog::error("failed to recv common header: {}", dbg_errcode(rc));
		return rc;
	}

	op_devlist_reply reply{};
	rc = usbip_net_recv(sockfd, &reply, sizeof(reply));
	if (rc < 0) {
		spdlog::error("failed to recv devlist: {}", dbg_errcode(rc));
		return rc;
	}

	PACK_OP_DEVLIST_REPLY(0, &reply);

	if (!reply.ndev) {
		spdlog::info("no exportable devices found on '{}'", host);
		return 0;
	}

	printf("Exportable USB devices\n");
	printf("======================\n");
	printf(" - %s\n", host);

	for (UINT32 i = 0; i < reply.ndev; ++i) {

		usbip_usb_device udev{};
		rc = usbip_net_recv(sockfd, &udev, sizeof(udev));
		if (rc < 0) {
			spdlog::error("failed to recv devlist: usbip_usb_device[{}]: {}", i, dbg_errcode(rc));
			return ERR_NETWORK;
		}
		usbip_net_pack_usb_device(0, &udev);

		auto &ids = get_ids();

		auto product_name = get_product(ids, udev.idVendor, udev.idProduct);
		auto class_name = get_class(ids, udev.bDeviceClass, udev.bDeviceSubClass, udev.bDeviceProtocol);

		printf("%11s: %s\n", udev.busid, product_name.c_str());
		printf("%11s: %s\n", "", udev.path);
		printf("%11s: %s\n", "", class_name.c_str());

		for (int j = 0; j < udev.bNumInterfaces; ++j) {
			usbip_usb_interface uintf{};
			rc = usbip_net_recv(sockfd, &uintf, sizeof(uintf));
			if (rc < 0) {
				spdlog::error("failed to recv devlist: usbip_usb_intf[{}]: {}", j, dbg_errcode(rc));
				return ERR_NETWORK;
			}

			usbip_net_pack_usb_interface(0, &uintf);

			auto csp = get_class(ids, uintf.bInterfaceClass, uintf.bInterfaceSubClass, uintf.bInterfaceProtocol);
			printf("%11s: %2d - %s\n", "", j, csp.c_str());
		}

		printf("\n");
	}

	return 0;
}

} // namespace


int list_exported_devices(const char *host)
{
	auto sock = usbip_net_tcp_connect(host, usbip_port);
	if (!sock) {
		spdlog::error("failed to connect a remote host '{}'", host);
		return 3;
	}
	spdlog::debug("connected to {}:{}\n", host, usbip_port);

	auto rc = get_exported_devices(host, sock.get());
	if (rc < 0) {
		spdlog::error("failed to get device list from '{}'", host);
		return 4;
	}

	return 0;
}