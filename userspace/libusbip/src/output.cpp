/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "..\output.h"
#include "output.h"

void libusbip::set_debug_output(const output_func_type &f)
{
        output_function = f;
}
