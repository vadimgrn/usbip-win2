/*
 *
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "usbip_windows.h"
#include "usbip_common.h"
#include "usbip_util.h"
#include "names.h"

#include <winsock2.h>

namespace
{

void cleanup_socket() noexcept
{
        [[maybe_unused]] auto err = WSACleanup();
        assert(!err);
}

auto init_socket() noexcept
{
	WSADATA	wsaData;

        if (auto err = WSAStartup(MAKEWORD(2, 2), &wsaData)) {
		dbg("WSAStartup error %#x", err);
		return false;
	}

	if (!(LOBYTE(wsaData.wVersion) == 2 && HIBYTE(wsaData.wVersion) == 2)) {
		dbg("cannot find a winsock 2.2 version");
                cleanup_socket();
                return false;
	}

	return true;
}

} // namespace


InitWinSock2::InitWinSock2() :
        m_ok(init_socket())
{
}

InitWinSock2::~InitWinSock2()
{
        if (m_ok) {
                cleanup_socket();
        }
}

InitUsbNames::InitUsbNames()
{
        auto path = get_module_dir() + "\\usb.ids";
        m_ok = !names_init(path.c_str());
}

InitUsbNames::~InitUsbNames()
{
        if (m_ok) {
                names_free();
        }
}
