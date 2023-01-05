/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\buffer.h>

namespace usbip
{

const ULONG pooltag = 'RTLF';

using unique_ptr = libdrv::unique_ptr<pooltag>;
using buffer = libdrv::buffer<pooltag>;

} // namespace usbip
