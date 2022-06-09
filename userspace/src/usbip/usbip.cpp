/*
 * command structure borrowed from udev
 * (git://git.kernel.org/pub/scm/linux/hotplug/udev.git)
 *
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
#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip.h"
#include "win_socket.h"
#include "file_ver.h"
#include "resource.h"
#include "usb_ids.h"

#include <string>

namespace
{

int usbip_help(int argc, char *argv[]);
int usbip_version(int argc, char *argv[]);

const char usbip_usage_string[] =
	"usbip [--debug] [--tcp-port PORT] [version]\n"
	"             [help] <command> <args>\n";

void usbip_usage()
{
	printf("usage: %s", usbip_usage_string);
}

struct command 
{
	const char *name;
	int (*fn)(int argc, char *argv[]);
	const char *help;
	void (*usage)();
};

const command cmds[] =
{
	{ "help", usbip_help},
	{ "version", usbip_version},
	{ "attach", usbip_attach, "Attach a remote USB device",	usbip_attach_usage },
	{ "detach", usbip_detach, "Detach a remote USB device", usbip_detach_usage },
	{ "list", usbip_list, "List remote USB devices", usbip_list_usage },
	{ "port", usbip_port_show, "Show imported USB devices", usbip_port_usage },
};

int usbip_help(int argc, char *argv[])
{
	if (argc > 1) {
		for (auto &c: cmds)
			if (std::string_view(c.name) == argv[1]) {
				if (c.usage) {
					c.usage();
                                } else {
					printf("no help for command: %s\n", argv[1]);
                                }
				return 0;
			}
		err("no help for invalid command: %s", argv[1]);
		return 1;
	}

	usbip_usage();
	printf("\n");
	for (auto &c: cmds) {
		if (c.help) {
			printf("  %-10s %s\n", c.name, c.help);
                }
        }
	printf("\n");
	return 0;
}

int usbip_version(int /*argc*/, [[maybe_unused]] char *argv[])
{
	FileVersion v;
	printf("usbip (%s)\n", v.GetFileVersion().c_str());
	return 0;
}

int run_command(const struct command *cmd, int argc, char *argv[])
{
	dbg("running command: %s", cmd->name);
	return cmd->fn(argc, argv);
}

auto get_ids_data()
{
	Resource r(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_USB_IDS), RT_RCDATA);
	assert(r);
	return r.str();
}

} // namespace


UsbIds& get_ids()
{
	static UsbIds ids(get_ids_data());
	assert(ids);
	return ids;
}

int main(int argc, char *argv[])
{
	const option opts[] = 
	{
		{ "debug",    no_argument,       nullptr, 'd' },
		{ "tcp-port", required_argument, nullptr, 't' },
		{}
	};

	int opt{};
	int rc = EXIT_FAILURE;

	usbip_progname = "usbip";
	usbip_use_stderr = true;

	for (opterr = 0; ; ) {
		opt = getopt_long(argc, argv, "+dt:", opts, nullptr);
		if (opt == -1) {
			break;
		}

		switch (opt) {
		case 'd':
			usbip_use_debug = true;
			break;
		case 't':
			usbip_setup_port_number(optarg);
			break;
		case '?':
			err("invalid option: %c", opt);
			[[fallthrough]];
		default:
			usbip_usage();
			return EXIT_FAILURE;
		}
	}

	usbip::InitWinSock2 ws2;
	if (!ws2) {
		err("cannot setup windows socket");
		return EXIT_FAILURE;
	}

	if (auto cmd = argv[optind]) {
		for (auto &c: cmds)
			if (std::string_view(c.name) == cmd) {
				argc -= optind;
				argv += optind;
				optind = 0;
				return run_command(&c, argc, argv);
			}
		err("invalid command: %s", cmd);
	}

	usbip_help(0, nullptr);
	return rc;
}
