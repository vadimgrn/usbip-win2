#pragma once

#include "generic_handle.h"

#include <cassert>
#include <functional>

#include <WinSock2.h>

namespace usbip
{

using Socket = GenericHandle<SOCKET, INVALID_SOCKET>;

template<>
inline void close_handle(Socket::Type s) noexcept
{
        [[maybe_unused]] auto ok = !closesocket(s);
        assert(ok);
}


} // namespace usbip


namespace std
{

template<>
struct std::hash<usbip::Socket>
{
        auto operator() (const usbip::Socket &s) const noexcept
        {
                std::hash<usbip::Socket::Type> f;
                return f(s.get());
        }
};

} // namespace std