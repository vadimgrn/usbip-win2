/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/win_handle.h>
#include <string>

namespace usbip
{

std::string GetLastErrorMsg(unsigned long msg_id = ~0UL);

Handle& get_vhci();

} // namespace usbip