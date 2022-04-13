#pragma once

#include "generic_handle.h"

#include <cassert>
#include <functional>

#include <WinSock2.h>

namespace usbip
{

using Socket = generic_handle<SOCKET, INVALID_SOCKET>;

template<>
inline void close_handle(Socket::type s) noexcept
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

template<>
struct std::hash<usbip::Socket>
{
        auto operator() (const usbip::Socket &s) const noexcept
        {
                std::hash<usbip::Socket::type> f;
                return f(s.get());
        }
};

inline void swap(usbip::Socket &a, usbip::Socket &b) noexcept
{
        a.swap(b);
}

} // namespace std