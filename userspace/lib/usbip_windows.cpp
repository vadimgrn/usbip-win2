/*
 *
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "usbip_windows.h"
#include "usbip_common.h"

#include <winsock2.h>

int init_socket()
{
	WSADATA	wsaData;

        if (auto err = WSAStartup(MAKEWORD(2, 2), &wsaData)) {
		dbg("WSAStartup error %#x", err);
		return -1;
	}

	if (!(LOBYTE(wsaData.wVersion) == 2 && HIBYTE(wsaData.wVersion) == 2)) {
		dbg("cannot find a winsock 2.2 version");
		WSACleanup();
		return -1;
	}

	return 0;
}

void cleanup_socket()
{
	[[maybe_unused]] auto err = WSACleanup();
        assert(!err);
}
