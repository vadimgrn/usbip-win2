/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <cstdio>
#include <format>
#include <io.h>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <windows.h>

namespace usbip::log
{

inline bool g_debug = false;

inline void set_debug(bool enable) noexcept
{
        g_debug = enable;
}

inline bool is_debug() noexcept
{
        return g_debug;
}

inline bool has_color_support() noexcept
{
        static const bool supported = [] {
                auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr)));
                if (handle == INVALID_HANDLE_VALUE) {
                        return false;
                }
                DWORD mode = 0;
                if (!GetConsoleMode(handle, &mode)) {
                        return false;
                }
                if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
                        if (!SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
                                return false;
                        }
                }
                return true;
        }();
        return supported;
}

inline void init() noexcept
{
        has_color_support();
}

inline void error(std::string_view msg)
{
        if (has_color_support()) {
                std::println(stderr, "\x1b[31merror\x1b[0m: {}", msg);
        } else {
                std::println(stderr, "error: {}", msg);
        }
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args)
{
        error(std::string_view(std::format(fmt, std::forward<Args>(args)...)));
}

inline void critical(std::string_view msg)
{
        if (has_color_support()) {
                std::println(stderr, "\x1b[1;31mcritical\x1b[0m: {}", msg);
        } else {
                std::println(stderr, "critical: {}", msg);
        }
}

template <typename... Args>
void critical(std::format_string<Args...> fmt, Args&&... args)
{
        critical(std::string_view(std::format(fmt, std::forward<Args>(args)...)));
}

inline void debug(std::string_view msg)
{
        if (g_debug) {
                if (has_color_support()) {
                        std::println(stderr, "\x1b[36mdebug\x1b[0m: {}", msg);
                } else {
                        std::println(stderr, "debug: {}", msg);
                }
        }
}

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args)
{
        if (g_debug) {
                debug(std::string_view(std::format(fmt, std::forward<Args>(args)...)));
        }
}

inline void debug_msg(std::string msg)
{
        debug(msg);
}

} // namespace usbip::log
