/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <functional>
#include <format>

namespace libusbip
{

using logger_fn = std::function<void(std::string)>;
inline logger_fn logger;

template<typename... Args>
inline void output(std::string_view fmt, Args&&... args)
{
        if (logger) {
                logger(vformat(fmt, std::make_format_args(args...)));
        }
}

template<typename... Args>
inline void output(std::wstring_view fmt, Args&&... args)
{
        if (logger) {
                logger(vformat(fmt, std::make_wformat_args(args...)));
        }
}

} // namespace libusbip
