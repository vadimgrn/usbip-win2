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

#include "getopt.h"
#include "usbip_network.h"
#include "usbip_vhci.h"
#include "usbip_common.h"
#include "dbgcode.h"

namespace
{

const char usbip_attach_usage_string[] =
"usbip attach <args>\n"
"    -r, --remote=<host>    The machine with exported USB devices\n"
"    -b, --busid=<busid>    Busid of the device on <host>\n"
"    -s, --serial=<USB serial>  (Optional) USB serial to be overwritten\n"
"    -t, --terse            show port number as a result\n";


int init(ioctl_usbip_vhci_plugin &r, const char *host, const char *busid, const char *serial)
{
        struct Data
        {
                char *dst;
                size_t len;
                const char *src;
        };

        const Data v[] = 
        {
                { r.service, ARRAYSIZE(r.service), usbip_port },
                { r.busid, ARRAYSIZE(r.busid), busid },
                { r.host, ARRAYSIZE(r.host), host },
                { r.serial, ARRAYSIZE(r.serial), serial },
        };

        for (auto &i: v) {
                if (auto src = i.src) {
                        if (auto err = strcpy_s(i.dst, i.len, src)) {
                                dbg("'%s' copy error #%d", src, err);
                                return ERR_GENERAL;
                        }
                }
        }

        return 0;
}

int import_device(const char *host, const char *busid, const char *serial)
{
        ioctl_usbip_vhci_plugin r{};
        if (auto err = init(r, host, busid, serial)) {
                return err;
        }
        
        auto hdev = usbip_vhci_driver_open();
        if (!hdev) {
                dbg("failed to open vhci driver");
                return ERR_DRIVER;
        }

        if (!usbip_vhci_attach_device(hdev.get(), r)) {
                return ERR_GENERAL;
        }
               
        return r.port;
}

/*
 * @see vhci/plugin.cpp, make_error
 */
int attach_device(const char *host, const char *busid, const char *serial, bool terse)
{
        auto result = import_device(host, busid, serial);
        static_assert(sizeof(result) == sizeof(int32_t));

        if (int port = result & 0xFFFF) {

                assert(port > 0);
                assert(!(result >> 16));

                if (terse) {
                        printf("%d\n", port);
                } else {
                        printf("succesfully attached to port %d\n", port);
                }

                return 0;
        }

        result >>= 16;
        assert(result);

        if (result > ST_OK) { // <linux>/tools/usb/usbip/libsrc/usbip_common.c, op_common_status_strings
                switch (auto err = static_cast<op_status_t>(result)) {
                case ST_NA:
                        err("device not available");
                        break;
                case ST_DEV_BUSY:
                        err("device busy (already exported)");
                        break;
                case ST_DEV_ERR:
                        err("device in error state");
                        break;
                case ST_NODEV:
                        err("device not found by bus id: %s", busid);
                        break;
                case ST_ERROR:
                        err("unexpected response");
                        break;
                default:
                        err("failed to attach: #%d %s", err, dbg_opcode_status(err));
                }

        } else switch (auto err = static_cast<err_t>(result)) {
                case ERR_DRIVER:
                        err("vhci driver is not loaded");
                        break;
                case ERR_NETWORK:
                        err("can't connect to %s:%s", host, usbip_port);
                        break;
                case ERR_PORTFULL:
                        err("no available port");
                        break;
                case ERR_PROTOCOL:
                        err("protocol error");
                        break;
                case ERR_VERSION:
                        err("incompatible protocol version");
                        break;
                default:
                        err("failed to attach: #%d %s", err, dbg_errcode(err));
        }

        return 3;
}

} // namespace


void usbip_attach_usage()
{
        printf("usage: %s", usbip_attach_usage_string);
}

int usbip_attach(int argc, char *argv[])
{
	const option opts[] = 
        {
		{ "remote", required_argument, nullptr, 'r' },
		{ "busid", required_argument, nullptr, 'b' },
		{ "serial", optional_argument, nullptr, 's' },
		{ "terse", required_argument, nullptr, 't' },
		{}
	};

	char *host{};
	char *busid{};
        char *serial{};
        bool terse{};

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
			terse = true;
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
