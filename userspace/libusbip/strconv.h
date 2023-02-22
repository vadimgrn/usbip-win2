/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace usbip
{

std::vector<std::wstring> split_multiz(_In_ std::wstring_view str);

std::wstring utf8_to_wchar(_In_ std::string_view s);
std::string wchar_to_utf8(_In_ std::wstring_view ws);

std::wstring wformat_message(_In_ DWORD flags, _In_opt_ HMODULE module, _In_ DWORD msg_id, _In_ DWORD lang_id);

/*
 * Use for error codes returned by GetLastError, WSAGetLastError, etc.
 */
inline auto wformat_message(_In_ DWORD msg_id, _In_ DWORD lang_id = 0)
{
        return wformat_message(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, msg_id, lang_id);
}

/*
 * #include <system_error>
 * std::system_category().message(ERROR_INVALID_PARAMETER); // encoding is CP_ACP
 * For POSIX errno codes: std::generic_category().message(errno()).
 */
inline auto format_message(_In_ DWORD msg_id, _In_ DWORD lang_id = 0) 
{ 
        auto ws = wformat_message(msg_id, lang_id);
        return wchar_to_utf8(ws);
}

inline auto wformat_message(_In_opt_ HMODULE module, _In_ DWORD msg_id, _In_ DWORD lang_id = 0)
{
        return wformat_message(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM, module, msg_id, lang_id);
}

inline auto format_message(_In_opt_ HMODULE module, _In_ DWORD msg_id, _In_ DWORD lang_id = 0)
{
        auto ws = wformat_message(module, msg_id, lang_id);
        return wchar_to_utf8(ws);
}

} // namespace usbip
