/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "win_socket.h"

namespace
{

auto init_wsa() noexcept
{
        enum { MAJOR = 2, MINOR = 2 };
        WSADATA	wsaData;

        if (auto err = WSAStartup(MAKEWORD(MINOR, MAJOR), &wsaData)) {
                SetLastError(err);
                return false;
        }

        if (!(LOBYTE(wsaData.wVersion) == MINOR && HIBYTE(wsaData.wVersion) == MAJOR)) {
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
