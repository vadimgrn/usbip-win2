/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "dllspec.h"

#include <string>
#include <functional>

namespace libusbip
{

/*
 * @param utf-8 encoded message 
 */
using output_func_type = std::function<void(std::string)>;

/*
 * Set a function if you want to get debug messages from the library.
 */
USBIP_API void set_debug_output(const output_func_type &f);

} // namespace libusbip
