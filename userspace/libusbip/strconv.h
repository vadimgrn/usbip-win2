/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <string>

namespace usbip
{

std::wstring utf8_to_wchar(std::string_view s);
std::string wchar_to_utf8(std::wstring_view ws);

/*
 * Use for error codes returned by GetLastError, WSAGetLastError, etc.
 */
std::wstring wformat_message(unsigned long msg_id);

/*
 * #include <system_error>
 * std::system_category().message(ERROR_INVALID_PARAMETER); // encoding is CP_ACP
 */
inline auto format_message(unsigned long msg_id)
{
        auto msg = wformat_message(msg_id);
        return wchar_to_utf8(msg);
}

} // namespace usbip
