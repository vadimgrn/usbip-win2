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

#include <cstdlib>

namespace
{

int usbip_help(int argc, char *argv[]);
int usbip_version(int argc, char *argv[]);

auto &usbip_version_string = PACKAGE_STRING;

const char usbip_usage_string[] =
	"usbip [--debug] [--tcp-port PORT] [version]\n"
	"             [help] <command> <args>\n";

void usbip_usage()
{
	printf("usage: %s", usbip_usage_string);
}

struct command {
	const char *name;
	int (*fn)(int argc, char *argv[]);
	const char *help;
	void (*usage)(void);
};

const struct command cmds[] = {
	{ "help", usbip_help},
	{ "version", usbip_version},
	{ "attach", usbip_attach, "Attach a remote USB device",	usbip_attach_usage },
	{ "detach", usbip_detach, "Detach a remote USB device", usbip_detach_usage },
	{ "list", usbip_list, "List exportable or local USB devices", usbip_list_usage },
	{ "bind", usbip_bind, "Bind device to usbip stub driver", usbip_bind_usage },
	{ "unbind", usbip_unbind, "Unbind device from usbip stub driver", usbip_unbind_usage },
	{ "port", usbip_port_show, "Show imported USB devices", usbip_port_usage },
	{}
};

int usbip_help(int argc, char *argv[])
{
	const struct command *cmd;

	if (argc > 1) {
		int	i;

		for (i = 0; cmds[i].name != nullptr; i++)
			if (strcmp(cmds[i].name, argv[1]) == 0) {
				if (cmds[i].usage)
					cmds[i].usage();
				else
					printf("no help for command: %s\n", argv[1]);
				return 0;
			}
		err("no help for invalid command: %s", argv[1]);
		return 1;
	}

	usbip_usage();
	printf("\n");
	for (cmd = cmds; cmd->name != nullptr; cmd++)
		if (cmd->help != nullptr)
			printf("  %-10s %s\n", cmd->name, cmd->help);
	printf("\n");
	return 0;
}

int usbip_version(int argc, char *argv[])
{
	printf("usbip (%s)\n", usbip_version_string);
	return 0;
}

int run_command(const struct command *cmd, int argc, char *argv[])
{
	dbg("running command: %s", cmd->name);
	return cmd->fn(argc, argv);
}

} // namespace


int main(int argc, char *argv[])
{
	const option opts[] = {
		{ "debug",    no_argument,       nullptr, 'd' },
		{ "tcp-port", required_argument, nullptr, 't' },
		{}
	};

	char *cmd{};
	int opt{};
	int rc = 1;

	usbip_progname = "usbip";
	usbip_use_stderr = 1;

	opterr = 0;
	while (true) {
		opt = getopt_long(argc, argv, "+dt:", opts, nullptr);

		if (opt == -1)
			break;

		switch (opt) {
		case 'd':
			usbip_use_debug = 1;
			break;
		case 't':
			usbip_setup_port_number(optarg);
			break;
		case '?':
			err("invalid option: %c", opt);
			[[fallthrough]];
		default:
			usbip_usage();
			return 1;
		}
	}

        usbip::InitWinSock2 ws2;
	if (!ws2) {
		err("cannot setup windows socket");
		return EXIT_FAILURE;
	}

	cmd = argv[optind];
	if (cmd) {
		for (int i = 0; cmds[i].name; i++)
			if (!strcmp(cmds[i].name, cmd)) {
				argc -= optind;
				argv += optind;
				optind = 0;
				return run_command(&cmds[i], argc, argv);
			}
		err("invalid command: %s", cmd);
	} else {
		/* empty command */
		usbip_help(0, nullptr);
	}

	return rc;
}
