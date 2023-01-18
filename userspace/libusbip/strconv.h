/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <string>

namespace libusbip
{

std::wstring utf8_to_wchar(std::string_view str);
std::string to_utf8(std::wstring_view wstr);

} // namespace libusbip

