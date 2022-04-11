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

#include "usbip_windows.h"
#include "getopt.h"
#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip_vhci.h"
#include "dbgcode.h"
#include "usbip_dscr.h"
#include "start_xfer.h"
#include "usbip_xfer/usbip_xfer.h"

#include <cstdlib>

static const char usbip_attach_usage_string[] =
	"usbip attach <args>\n"
	"    -r, --remote=<host>    The machine with exported USB devices\n"
	"    -b, --busid=<busid>    Busid of the device on <host>\n"
	"    -s, --serial=<USB serial>  (Optional) USB serial to be overwritten\n"
	"    -t, --terse            show port number as a result\n";

void usbip_attach_usage()
{
	printf("usage: %s", usbip_attach_usage_string);
}

static int import_device(vhci_pluginfo_t *pluginfo, usbip::Handle &handle)
{
	auto hdev = usbip_vhci_driver_open();
	if (!hdev) {
		dbg("failed to open vhci driver");
		return ERR_DRIVER;
	}

	auto rc = usbip_vhci_attach_device(hdev.get(), pluginfo);
	if (rc < 0) {
		if (rc == ERR_PORTFULL) {
			dbg("no free port");
		} else {
			dbg("failed to attach device: %d", rc);
		}
		return rc;
	}

        handle = std::move(hdev);
	return pluginfo->port;
}

static struct vhci_pluginfo_t*
build_pluginfo(SOCKET sockfd, unsigned devid)
{
	USHORT wTotalLength = 0;

	if (fetch_conf_descriptor(sockfd, devid, nullptr, &wTotalLength) < 0) {
		dbg("failed to get configuration descriptor size");
		return nullptr;
	}

	vhci_pluginfo_t *pluginfo = nullptr;
	unsigned long pluginfo_size = sizeof(*pluginfo) + wTotalLength - sizeof(pluginfo->dscr_conf);

	pluginfo = (vhci_pluginfo_t*)malloc(pluginfo_size);
	if (!pluginfo) {
		dbg("out of memory or invalid vhci pluginfo size");
		return nullptr;
	}
	
	pluginfo->size = pluginfo_size;
	pluginfo->devid = devid;

	if (fetch_device_descriptor(sockfd, devid, &pluginfo->dscr_dev) < 0) {
		dbg("failed to fetch device descriptor");
		free(pluginfo);
		return nullptr;
	}

	if (fetch_conf_descriptor(sockfd, devid, &pluginfo->dscr_conf, &wTotalLength) < 0) {
		dbg("failed to fetch configuration descriptor");
		free(pluginfo);
		return nullptr;
	}

	return pluginfo;
}

static int query_import_device(SOCKET sockfd, const char *busid, usbip::Handle &hdev, const char *serial)
{
	/* send a request */
	int rc = usbip_net_send_op_common(sockfd, OP_REQ_IMPORT, 0);
	if (rc < 0) {
		dbg("failed to send common header: %s", dbg_errcode(rc));
		return ERR_NETWORK;
	}

	op_import_request request{};
	strcpy_s(request.busid, sizeof(request.busid), busid);

	PACK_OP_IMPORT_REQUEST(0, &request);

	rc = usbip_net_send(sockfd, &request, sizeof(request));
	if (rc < 0) {
		dbg("failed to send import request: %s", dbg_errcode(rc));
		return ERR_NETWORK;
	}

	uint16_t code = OP_REP_IMPORT;
	int status = 0;

	/* recieve a reply */
	rc = usbip_net_recv_op_common(sockfd, &code, &status);
	if (rc < 0) {
		dbg("failed to recv common header: %s", dbg_errcode(rc));
		if (rc == ERR_STATUS) {
			dbg("op code error: %s", dbg_opcode_status(status));

			switch (status) {
			case ST_NODEV:
				return ERR_NOTEXIST;
			case ST_DEV_BUSY:
				return ERR_EXIST;
			default:
				break;
			}
		}
		return rc;
	}

	op_import_reply reply{};

	rc = usbip_net_recv(sockfd, &reply, sizeof(reply));
	if (rc < 0) {
		dbg("failed to recv import reply: %s", dbg_errcode(rc));
		return ERR_NETWORK;
	}

	PACK_OP_IMPORT_REPLY(0, &reply);

	/* check the reply */
	if (strncmp(reply.udev.busid, busid, sizeof(reply.udev.busid))) {
		dbg("recv different busid: %s", reply.udev.busid);
		return ERR_PROTOCOL;
	}

	unsigned int devid = reply.udev.busnum << 16 | reply.udev.devnum;

	struct vhci_pluginfo_t *pluginfo = build_pluginfo(sockfd, devid);
	if (!pluginfo) {
		return ERR_GENERAL;
	}

	if (serial) {
		mbstowcs_s(nullptr, pluginfo->wserial, sizeof(pluginfo->wserial)/sizeof(*pluginfo->wserial), serial, _TRUNCATE);
	} else {
		pluginfo->wserial[0] = L'\0';
	}

	/* import a device */
	rc = import_device(pluginfo, hdev);
	free(pluginfo);
	return rc;
}

static int
attach_device(const char *host, const char *busid, const char *serial, BOOL terse)
{
	auto sock = usbip_net_tcp_connect(host, usbip_port_string);
	if (!sock) {
		err("failed to connect a remote host: %s", host);
		return 2;
	}

        usbip::Handle hdev;
        auto rhport = query_import_device(sock.get(), busid, hdev, serial);
	if (rhport < 0) {
		switch (rhport) {
		case ERR_DRIVER:
			err("vhci driver is not loaded");
			break;
		case ERR_EXIST:
			err("already used bus id: %s", busid);
			break;
		case ERR_NOTEXIST:
			err("non-existent bus id: %s", busid);
			break;
		case ERR_PORTFULL:
			err("no available port");
			break;
		default:
			err("failed to attach");
			break;
		}
		return 3;
	}

	auto ret = start_xfer(hdev.get(), sock.get(), true);
	if (!ret) {
		if (terse) {
			printf("%d\n", rhport);
		} else {
			printf("succesfully attached to port %d\n", rhport);
		}
	} else {
		switch (ret) {
		case ERR_NOTEXIST:
			err("%s not found", usbip_xfer_binary());
			break;
		default:
			err("failed to run %s", usbip_xfer_binary());
			break;
		}
		ret = 4;
	}

	return ret;
}

int usbip_attach(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "remote", required_argument, nullptr, 'r' },
		{ "busid", required_argument, nullptr, 'b' },
		{ "serial", optional_argument, nullptr, 's' },
		{ "terse", required_argument, nullptr, 't' },
		{ nullptr, 0, nullptr, 0 }
	};

	char	*host = nullptr;
	char	*busid = nullptr;
	char	*serial = nullptr;
	BOOL	terse = FALSE;

	while (true) {
		int opt = getopt_long(argc, argv, "r:b:s:t", opts, nullptr);

		if (opt == -1)
			break;

		switch (opt) {
		case 'r':
			host = optarg;
			break;
		case 'b':
			busid = optarg;
			break;
		case 's':
			serial = optarg;
			break;
		case 't':
			terse = TRUE;
			break;
		default:
			err("invalid option: %c", opt);
			usbip_attach_usage();
			return 1;
		}
	}

	if (!host) {
		err("empty remote host");
		usbip_attach_usage();
		return 1;
	}
	
	if (!busid) {
		err("empty busid");
		usbip_attach_usage();
		return 1;
	}

	return attach_device(host, busid, serial, terse);
}
