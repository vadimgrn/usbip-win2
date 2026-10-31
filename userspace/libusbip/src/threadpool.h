/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <windows.h>

namespace usbip
{

PTP_CALLBACK_ENVIRON get_threadpool_environ() noexcept;

} // namespace usbip
