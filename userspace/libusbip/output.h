/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <functional>
#include <format>

#include "strconv.h"

namespace libusbip
{

/*
 * @param utf-8 encoded message 
 */
inline std::function<void(std::string)> output_function;

template<typename... Args>
inline void output(std::string_view fmt, Args&&... args)
{
        if (output_function) {
                auto s = vformat(fmt, std::make_format_args(args...));
                output_function(std::move(s));
        }
}

template<typename... Args>
inline void output(std::wstring_view fmt, Args&&... args)
{
        if (output_function) {
                auto ws = vformat(fmt, std::make_wformat_args(args...));
                output_function(usbip::wchar_to_utf8(ws));
        }
}

} // namespace libusbip
