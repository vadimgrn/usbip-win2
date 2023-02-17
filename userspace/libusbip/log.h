/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <spdlog\spdlog.h>

namespace libusbip
{

/*
 * Do not use logger directly, call output().
 */
inline std::shared_ptr<spdlog::logger> logger;
inline auto output_level = spdlog::level::debug;

template<typename... Args>
inline void output(spdlog::format_string_t<Args...> fmt, Args&&... args)
{
        if (auto obj = logger.get()) {
                obj->log(output_level, fmt, std::forward<Args>(args)...);
        }
}

template<typename... Args>
inline void output(spdlog::wformat_string_t<Args...> fmt, Args&&... args)
{
        if (auto obj = logger.get()) {
                obj->log(output_level, fmt, std::forward<Args>(args)...);
        }
}

} // namespace libusbip
