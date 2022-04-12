/*
 *
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include <ws2tcpip.h>
#include <signal.h>

#include "../config.h"
#include "usbipd.h"
#include "usbip_network.h"
#include "getopt.h"
#include "usbip_windows.h"

#undef  PROGNAME
#define PROGNAME "usbipd"

#define MAIN_LOOP_TIMEOUT 10

extern SOCKET *get_listen_sockfds(int family);
extern void accept_request(SOCKET *sockfds, fd_set *pfds);

static auto &usbip_version_string = PACKAGE_STRING;

static const char usbipd_help_string[] =
	"usage: usbipd [options]\n"
	"\n"
	"	-4, --ipv4\n"
	"		Bind to IPv4. Default is both.\n"
	"\n"
	"	-6, --ipv6\n"
	"		Bind to IPv6. Default is both.\n"
	"\n"
	"	-d, --debug\n"
	"		Print debugging information.\n"
	"\n"
	"	-tPORT, --tcp-port PORT\n"
	"		Listen on TCP/IP port PORT.\n"
	"\n"
	"	-h, --help\n"
	"		Print this help.\n"
	"\n"
	"	-v, --version\n"
	"		Show version.\n";

enum {
	cmd_standalone_mode = 1,
	cmd_help,
	cmd_version
}; 

static int cmd = cmd_standalone_mode;

static int	family = AF_UNSPEC;

static void
usbipd_help(void)
{
	printf("%s\n", usbipd_help_string);
}

static void
signal_handler(int i)
{
	dbg("received '%d' signal", i);
}

static void
set_signal(void)
{
	signal(SIGINT, signal_handler);
}

static int
setup_fds(SOCKET *sockfds, fd_set *pfds)
{
	int	i;

	FD_ZERO(pfds);
	for (i = 0; sockfds[i] != INVALID_SOCKET; i++) {
		FD_SET(sockfds[i], pfds);
	}
	return i;
}

static int
do_standalone_mode(void)
{
	fd_set	fds;
	SOCKET	*sockfds;
	int	n_sockfds;
	int	ret = 0;

	InitWinSock2 ws2;
        if (!ws2) {
                err("failed to init WinSock2");
                return 2;
        }

	set_signal();

	info("starting " PROGNAME " (%s)", usbip_version_string);

	sockfds = get_listen_sockfds(family);
	if (sockfds == nullptr) {
		err("failed to open a listening socket");
		return 2;
	}

	n_sockfds = setup_fds(sockfds, &fds);
	while (TRUE) {
		struct timeval	timeout;
		int rc;

		timeout.tv_sec = MAIN_LOOP_TIMEOUT;
		timeout.tv_usec = 0;
		fds.fd_count = n_sockfds;
		rc = select(n_sockfds, &fds, nullptr, NULL, &timeout);
		if (rc == SOCKET_ERROR) {
			dbg("failed to select: err: %d", WSAGetLastError());
			err("operation halted by socket error");
			ret = 2;
			break;
		}
		else if (rc > 0) {
			accept_request(sockfds, &fds);
		}
	}

	info("shutting down " PROGNAME);

	return 0;
}

static BOOL
parse_args(int argc, char *argv[])
{
	const struct option longopts[] = {
	{ "ipv4",     no_argument,       nullptr, '4' },
	{ "ipv6",     no_argument,       nullptr, '6' },
	{ "debug",    no_argument,       nullptr, 'd' },
	{ "device",   no_argument,       nullptr, 'e' },
	{ "pid",      optional_argument, nullptr, 'P' },
	{ "tcp-port", required_argument, nullptr, 't' },
	{ "help",     no_argument,       nullptr, 'h' },
	{ "version",  no_argument,       nullptr, 'v' },
	{ nullptr,	      0,                 NULL,  0 }
	};
	BOOL	ipv4 = FALSE, ipv6 = FALSE;

	for (;;) {
		int	opt;

		opt = getopt_long(argc, argv, "46Ddt:hv", longopts, nullptr);

		if (opt == -1)
			break;

		switch (opt) {
		case '4':
			ipv4 = TRUE;
			break;
		case '6':
			ipv6 = TRUE;
			break;
		case 'd':
			usbip_use_debug = 1;
			break;
		case 'h':
			cmd = cmd_help;
			break;
		case 't':
			usbip_setup_port_number(optarg);
			break;
		case 'v':
			cmd = cmd_version;
			break;
		case '?':
			usbipd_help();
		default:
			return FALSE;
		}
	}

	/*
	 * To suppress warnings on systems with bindv6only disabled
	 * (default), we use seperate sockets for IPv6 and IPv4 and set
	 * IPV6_V6ONLY on the IPv6 sockets.
	 */
	if (ipv4 && !ipv6)
		family = AF_INET;
	else if (!ipv4 && ipv6)
		family = AF_INET6;
	return TRUE;
}

int
main(int argc, char *argv[])
{
	usbip_progname = "usbipd";
	usbip_use_stderr = 1;

	if (!parse_args(argc, argv))
		return EXIT_FAILURE;

	switch (cmd) {
	case cmd_standalone_mode:
		return do_standalone_mode();
	case cmd_version:
		printf(PROGNAME " (%s)\n", usbip_version_string);
		return EXIT_SUCCESS;
	case cmd_help:
		usbipd_help();
		return EXIT_SUCCESS;
	default:
		usbipd_help();
		return EXIT_FAILURE;
	}
}