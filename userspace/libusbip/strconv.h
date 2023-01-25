/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <string>

namespace usbip
{

std::wstring utf8_to_wchar(std::string_view str);
std::string wchar_to_utf8(std::wstring_view wstr);

std::string format_message(unsigned long msg_id);
std::wstring wformat_message(unsigned long msg_id);

} // namespace usbip

