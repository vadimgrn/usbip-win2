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
#include <libusbip\dbgcode.h>

#include <spdlog\spdlog.h>

namespace
{

using namespace usbip;

auto init(vhci::ioctl_plugin &r, const attach_args &args)
{
        auto service = std::to_string(global_args.tcp_port);

        struct {
                char *dst;
                size_t len;
                const std::string &src;
        } const v[] = {
                { r.busid, ARRAYSIZE(r.busid), args.busid },
                { r.service, ARRAYSIZE(r.service), service },
                { r.host, ARRAYSIZE(r.host), args.remote },
                { r.serial, ARRAYSIZE(r.serial), args.serial },
        };

        for (auto &i: v) {
                if (!i.src.empty()) {
                        if (auto err = strcpy_s(i.dst, i.len, i.src.c_str())) {
                                spdlog::error("strcpy_s('{}') error #{}", i.src, err);
                                return ERR_GENERAL;
                        }
                }
        }

        return ERR_NONE;
}

auto import_device(const attach_args &args)
{
        vhci::ioctl_plugin r{};
        if (auto err = init(r, args)) {
                return make_error(err);
        }

        auto dev = vhci::open();
        if (!dev) {
                spdlog::error("failed to open vhci device");
                return make_error(ERR_DRIVER);
        }

        if (!vhci::attach(dev.get(), r)) {
                return make_error(ERR_GENERAL);
        }

        return r.port;
}

constexpr auto get_port(int result) { return result & 0xFFFF; }
constexpr auto get_error(int result) { return result >> 16; }

} // namespace


/*
 * @see vhci/plugin.cpp, make_error
 */
int usbip::cmd_attach(attach_args &r)
{
        auto result = import_device(r);

        if (int port = get_port(result)) {

                assert(port > 0);
                assert(!get_error(result));

                if (r.terse) {
                        printf("%d\n", port);
                } else {
                        printf("succesfully attached to port %d\n", port);
                }

                return 0;
        }

        result = get_error(result);
        assert(result);

        if (result > ST_OK) { // <linux>/tools/usb/usbip/libsrc/usbip_common.c, op_common_status_strings
                switch (auto err = static_cast<op_status_t>(result)) {
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
                        spdlog::error("device not found by bus id: {}", r.busid);
                        break;
                case ST_ERROR:
                        spdlog::error("unexpected response");
                        break;
                default:
                        spdlog::error("attach error #{} {}", err, libusbip::dbg_opcode_status(err));
                }

        } else switch (auto err = static_cast<err_t>(result)) {
        case ERR_DRIVER:
                spdlog::error("vhci driver is not loaded");
                break;
        case ERR_NETWORK:
                spdlog::error("can't connect to {}:{}", r.remote, global_args.tcp_port);
                break;
        case ERR_PORTFULL:
                spdlog::error("no available port");
                break;
        case ERR_PROTOCOL:
                spdlog::error("protocol error");
                break;
        case ERR_VERSION:
                spdlog::error("incompatible protocol version");
                break;
        default:
                spdlog::error("attach error #{} {}", err, libusbip::dbg_errcode(err));
        }

        return 3;
}
