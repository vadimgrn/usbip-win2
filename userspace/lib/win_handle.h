#pragma once

#include "generic_handle.h"

#include <cassert>
#include <functional>

#include <windows.h>

namespace usbip
{

using Handle = GenericHandle<HANDLE, INVALID_HANDLE_VALUE>;

template<>
inline void close_handle(Handle::Type h) noexcept
{
        [[maybe_unused]] auto ok = CloseHandle(h);
        assert(ok);
}

} // namespace usbip


namespace std
{

template<>
struct std::hash<usbip::Handle>
{
        auto operator() (const usbip::Handle &h) const noexcept
        {
                std::hash<usbip::Handle::Type> f;
                return f(h.get());
        }
};

} // namespace std
