/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/unique_ptr.h>

namespace usbip
{

const ULONG pooltag = 'ICHV';
using unique_ptr = libdrv::unique_ptr<pooltag>;

} // namespace usbip
