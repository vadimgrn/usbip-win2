/*
 * Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/unique_ptr.h>

namespace usbip
{

const ULONG pooltag = 'ICHV';
using unique_ptr = libdrv::unique_ptr<pooltag>;

} // namespace usbip
