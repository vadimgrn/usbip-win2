/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <string>
#include <functional>

namespace libusbip
{

/*
 * Assign a function to this variable if you want to get debug messages from the library.
 * @param utf-8 encoded message 
 */
inline std::function<void(std::string)> output_function;

} // namespace libusbip
