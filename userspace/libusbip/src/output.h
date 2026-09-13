/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "../output.h"
#include "strconv.h"

#include <format>

namespace libusbip
{

inline output_func_type output_function;

template<typename... Args>
inline void output(std::format_string<Args...> fmt, Args&&... args)
{
        if (output_function) {
                output_function(std::format(fmt, std::forward<Args>(args)...));
        }
}

template<typename... Args>
inline void output(std::wformat_string<Args...> fmt, Args&&... args)
{
        if (output_function) {
                auto ws = std::format(fmt, std::forward<Args>(args)...);
                output_function(usbip::wchar_to_utf8_or(ws));
        }
}

} // namespace libusbip
