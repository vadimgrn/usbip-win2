/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "..\output.h"
#include "output.h"

void libusbip::set_debug_output(const output_func_type &f)
{
        output_function = f;
}

auto libusbip::get_debug_output() noexcept -> const output_func_type&
{
        return output_function;
}
