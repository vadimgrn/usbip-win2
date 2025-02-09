/*
 * Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "dllspec.h"
#include "generic_handle.h"

#include <cassert>

/*
 * Define WIN32_LEAN_AND_MEAN for the project 
 * to prevent inclusion of winsock.h in windows.h
 */
#include <WinSock2.h>

namespace usbip
{

struct SocketTag {};
using Socket = generic_handle<SOCKET, SocketTag, INVALID_SOCKET>;

template<>
inline void close_handle(_In_ Socket::type s, _In_ Socket::tag_type) noexcept
{
        [[maybe_unused]] auto err = closesocket(s);
        assert(!err);
}


struct WSAEventTag {};
using WSAEvent = generic_handle<WSAEVENT, WSAEventTag, WSA_INVALID_EVENT>;

template<>
inline void close_handle(_In_ WSAEvent::type evt, _In_ WSAEvent::tag_type) noexcept
{
        [[maybe_unused]] auto ok = WSACloseEvent(evt);
        assert(ok);
}


class USBIP_API InitWinSock2
{
public:
        InitWinSock2();
        ~InitWinSock2();

        InitWinSock2(const InitWinSock2&) = delete;
        InitWinSock2& operator=(const InitWinSock2&) = delete;

        explicit operator bool() const noexcept { return m_ok; }
        auto operator !() const noexcept { return !m_ok; }

private:
        bool m_ok{};
};

} // namespace usbip
