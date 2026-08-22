/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "generic_handle_ex.h"
#include <functional>

namespace std
{

using usbip::generic_handle;
using usbip::swap;

template<typename HandleTraits>
struct hash<generic_handle<HandleTraits>>
{
        auto operator() (const generic_handle<HandleTraits> &h) const noexcept
        {
                auto val = h.get();
                hash<decltype(val)> f; // typename generic_handle<HandleTraits>::type
                return f(val);
        }
};

} // namespace std
