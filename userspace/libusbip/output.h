/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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

/**
 * Set a function if you want to get debug messages from the library.
 * Must be called during single-threaded initialization prior to invoking other
 * library functions. The callback itself must be thread-safe as debug messages
 * may be emitted concurrently from multiple worker threads.
 */
USBIP_API void set_debug_output(const output_func_type &f);

/*
 * Get installed debug function.
 */
USBIP_API const output_func_type& get_debug_output() noexcept;

} // namespace libusbip
