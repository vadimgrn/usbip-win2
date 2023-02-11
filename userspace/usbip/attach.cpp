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

#include <libusbip\vhci.h>
#include <libusbip\errmsg.h>

#include <spdlog\spdlog.h>

bool usbip::cmd_attach(void *p)
{
        auto dev = vhci::open();
        if (!dev) {
                return false;
        }

        auto &args = *reinterpret_cast<attach_args*>(p);

        vhci::ioctl_plugin_hardware r{};
        if (!fill(r, args.remote, global_args.tcp_port, args.busid)) {
                return false;
        }

        if (auto port = vhci::attach(dev.get(), r)) {
                if (args.terse) {
                        printf("%d\n", port);
                } else {
                        printf("succesfully attached to port %d\n", port);
                }
                return true;
        }

        if (auto err = r.get_err()) {
                switch (err) {
                case ERR_ADDRINFO:
                        spdlog::error("can't get address info for {}:{}", args.remote, global_args.tcp_port);
                        break;
                case ERR_CONNECT:
                        spdlog::error("can't connect to {}:{}", args.remote, global_args.tcp_port);
                        break;
                case ERR_NETWORK:
                        spdlog::error("network error");
                        break;
                case ERR_PROTOCOL:
                        spdlog::error("protocol error");
                        break;
                case ERR_VERSION:
                        spdlog::error("incompatible protocol version");
                        break;
                case ERR_PORTFULL:
                        spdlog::error("no available port");
                        break;
                default:
                        spdlog::error("attach error #{} {}", err, errt_str(err));
                }
        } else switch (auto st = r.get_status()) { // <linux>/tools/usb/usbip/libsrc/usbip_common.c, op_common_status_strings
                case ST_NA:
                        spdlog::error("device not available");
                        break;
                case ST_DEV_BUSY:
                        spdlog::error("device busy (already exported)");
                        break;
                case ST_DEV_ERR:
                        spdlog::error("device in error state");
                        break;
                case ST_NODEV:
                        spdlog::error("device not found by bus-id '{}'", r.busid);
                        break;
                case ST_ERROR:
                        spdlog::error("unexpected response");
                        break;
                default:
                        assert(st != ST_OK);
                        spdlog::error("attach status #{} {}", st, op_status_str(st));
        }

        return false;
}
