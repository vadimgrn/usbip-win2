/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "generic_handle.h"

#include <cassert>
#include <functional>

#include <WinSock2.h>

namespace usbip
{

struct SocketTag {};
using Socket = generic_handle<SOCKET, SocketTag, INVALID_SOCKET>;

template<>
inline void close_handle(Socket::type s, Socket::tag_type) noexcept
{
        [[maybe_unused]] auto ok = !closesocket(s);
        assert(ok);
}

class InitWinSock2
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


namespace std
{

using usbip::Socket;

template<>
struct std::hash<Socket>
{
        auto operator() (const Socket &s) const noexcept
        {
                std::hash<Socket::type> f;
                return f(s.get());
        }
};

inline void swap(Socket &a, Socket &b) noexcept
{
        a.swap(b);
}

} // namespace std