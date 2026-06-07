/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/unique_ptr.h>

namespace usbip
{

using unique_ptr = libdrv::unique_ptr<'RTLF'>;

} // namespace usbip
