/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "generic_handle_ex.h"
#include <functional>

namespace std
{

using usbip::generic_handle;
using usbip::swap;

template<typename Handle, typename Tag, auto NoneValue>
struct std::hash<generic_handle<Handle, Tag, NoneValue>>
{
        auto operator() (const generic_handle<Handle, Tag, NoneValue> &h) const noexcept
        {
                std::hash<h.type> f;
                return f(h.get());
        }
};

} // namespace std
