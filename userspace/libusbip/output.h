/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <functional>
#include <format>

namespace libusbip
{

inline std::function<void(std::string)> output_function;
inline std::function<void(std::wstring)> woutput_function;

template<typename... Args>
inline void output(std::string_view fmt, Args&&... args)
{
        if (output_function) {
                output_function(vformat(fmt, std::make_format_args(args...)));
        }
}

template<typename... Args>
inline void output(std::wstring_view fmt, Args&&... args)
{
        if (woutput_function) {
                woutput_function(vformat(fmt, std::make_wformat_args(args...)));
        }
}

} // namespace libusbip
