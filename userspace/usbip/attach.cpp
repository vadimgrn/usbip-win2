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
#include <usbip\vhci.h>

#include <spdlog\spdlog.h>

#include <system_error>

bool usbip::cmd_attach(void *p)
{
        auto dev = vhci::open();
        if (!dev) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        auto &args = *reinterpret_cast<attach_args*>(p);
        vhci::ioctl_plugin_hardware r;

        if (auto err = init(r, args.remote, global_args.tcp_port, args.busid)) {
                auto msg = std::generic_category().message(err);
                spdlog::error("#{} {}", err, msg);
                return false;
        }

        auto port = vhci::attach(dev.get(), r);
        if (!port) {
                spdlog::error(GetLastErrorMsg());
                return false;
        }

        if (args.terse) {
                printf("%d\n", port);
        } else {
                printf("succesfully attached to port %d\n", port);
        }

        return true;
}
