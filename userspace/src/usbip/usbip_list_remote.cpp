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

#include <ws2tcpip.h>

#include "usbip_common.h"
#include "usbip_network.h"
#include "dbgcode.h"

#include "usbip_windows.h"

static int get_exported_devices(const char *host, SOCKET sockfd)
{
	struct op_devlist_reply reply;
	uint16_t code = OP_REP_DEVLIST;
	unsigned int i;
	int	status;
	int	rc;

	rc = usbip_net_send_op_common(sockfd, OP_REQ_DEVLIST, 0);
	if (rc < 0) {
		dbg("failed to send common header: %s", dbg_errcode(rc));
		return ERR_NETWORK;
	}

	rc = usbip_net_recv_op_common(sockfd, &code, &status);
	if (rc < 0) {
		dbg("failed to recv common header: %s", dbg_errcode(rc));
		return rc;
	}

	memset(&reply, 0, sizeof(reply));
	rc = usbip_net_recv(sockfd, &reply, sizeof(reply));
	if (rc < 0) {
		dbg("failed to recv devlist: %s", dbg_errcode(rc));
		return rc;
	}

	PACK_OP_DEVLIST_REPLY(0, &reply);
	dbg("exportable devices: %d\n", reply.ndev);

	if (reply.ndev == 0) {
		info("no exportable devices found on %s", host);
		return 0;
	}

	printf("Exportable USB devices\n");
	printf("======================\n");
	printf(" - %s\n", host);

	for (i = 0; i < reply.ndev; i++) {
		char product_name[100];
		char class_name[100];
		struct usbip_usb_device udev;
		int j;

		memset(&udev, 0, sizeof(udev));

		rc = usbip_net_recv(sockfd, &udev, sizeof(udev));
		if (rc < 0) {
			dbg("failed to recv devlist: usbip_usb_device[%d]: %s", i, dbg_errcode(rc));
			return ERR_NETWORK;
		}
		usbip_net_pack_usb_device(0, &udev);

		usbip_names_get_product(product_name, sizeof(product_name),
					udev.idVendor, udev.idProduct);
		usbip_names_get_class(class_name, sizeof(class_name),
				      udev.bDeviceClass, udev.bDeviceSubClass,
				      udev.bDeviceProtocol);

		printf("%11s: %s\n", udev.busid, product_name);
		printf("%11s: %s\n", "", udev.path);
		printf("%11s: %s\n", "", class_name);

		for (j = 0; j < udev.bNumInterfaces; j++) {
			struct usbip_usb_interface uintf;

			rc = usbip_net_recv(sockfd, &uintf, sizeof(uintf));
			if (rc < 0) {
				dbg("failed to recv devlist: usbip_usb_intf[%d]: %s", j, dbg_errcode(rc));
				return ERR_NETWORK;
			}

			usbip_net_pack_usb_interface(0, &uintf);

			usbip_names_get_class(class_name, sizeof(class_name),
					      uintf.bInterfaceClass,
					      uintf.bInterfaceSubClass,
					      uintf.bInterfaceProtocol);

			printf("%11s: %2d - %s\n", "", j, class_name);
		}

		printf("\n");
	}

	return 0;
}

int
list_exported_devices(const char *host)
{
	int rc;
	SOCKET sockfd;

	sockfd = usbip_net_tcp_connect(host, usbip_port_string);
	if (sockfd == INVALID_SOCKET) {
		err("failed to connect a remote host: %s", host);
		return 3;
	}
	dbg("connected to %s:%s\n", host, usbip_port_string);

	rc = get_exported_devices(host, sockfd);
	if (rc < 0) {
		err("failed to get device list from %s", host);
		return 4;
	}

	closesocket(sockfd);

	return 0;
}