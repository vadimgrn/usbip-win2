/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "../win_socket.h"
#include "output.h"

namespace
{

auto init_wsa() noexcept
{
        const BYTE MAJOR = 2;
        const BYTE MINOR = 2;

        WSADATA	wsaData;
        if (auto err = WSAStartup(MAKEWORD(MAJOR, MINOR), &wsaData)) {
                SetLastError(err);
                libusbip::output("WSAStartup version {}.{} error {:#x}", MAJOR, MINOR, err);
                return false;
        }

        if (!(LOBYTE(wsaData.wVersion) == MAJOR && HIBYTE(wsaData.wVersion) == MINOR)) {
                libusbip::output("WinSock2 version {}.{} is not available", MAJOR, MINOR);
                WSACleanup();
                SetLastError(WSAEINVAL);
                return false;
        }

        return true;
}

} // namespace


usbip::InitWinSock2::InitWinSock2() :
        m_ok(init_wsa())
{
}

usbip::InitWinSock2::~InitWinSock2()
{
        if (m_ok) {
                [[maybe_unused]] auto err = WSACleanup();
                assert(!err);
        }
}
