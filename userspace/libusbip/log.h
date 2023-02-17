/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <spdlog\spdlog.h>
#include <spdlog\sinks\null_sink.h>

namespace libusbip
{

inline auto log = spdlog::null_logger_st("libusbip");

} // namespace libusbip
