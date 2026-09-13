/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "generic_handle_ex.h"
#include <functional>

namespace std
{

template<typename HandleTraits>
struct hash<usbip::generic_handle<HandleTraits>>
{
        auto operator() (const usbip::generic_handle<HandleTraits> &h) const noexcept
        {
                auto val = h.get();
                hash<decltype(val)> f; // typename usbip::generic_handle<HandleTraits>::type
                return f(val);
        }
};

} // namespace std
