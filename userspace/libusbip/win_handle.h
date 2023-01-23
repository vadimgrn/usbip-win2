/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "generic_handle.h"

#include <cassert>
#include <functional>

#include <windows.h>

namespace libusbip
{

struct HandleTag {};
using Handle = generic_handle<HANDLE, HandleTag, INVALID_HANDLE_VALUE>;

template<>
inline void close_handle(Handle::type h, Handle::tag_type) noexcept
{
        [[maybe_unused]] auto ok = CloseHandle(h);
        assert(ok);
}

} // namespace libusbip


namespace std
{

using libusbip::Handle;

template<>
struct std::hash<Handle>
{
        auto operator() (const Handle &h) const noexcept
        {
                std::hash<Handle::type> f;
                return f(h.get());
        }
};

inline void swap(Handle &a, Handle &b) noexcept
{
        a.swap(b);
}

} // namespace std
