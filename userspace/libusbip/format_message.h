/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "dllspec.h"

#include <windows.h>
#include <string>

namespace usbip
{

USBIP_API std::wstring wformat_message(_In_ DWORD flags, _In_opt_ HMODULE module, _In_ DWORD msg_id, _In_ DWORD lang_id);

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
USBIP_API std::string format_message(_In_ DWORD msg_id, _In_ DWORD lang_id = 0);

inline auto wformat_message(_In_opt_ HMODULE module, _In_ DWORD msg_id, _In_ DWORD lang_id = 0)
{
        return wformat_message(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM, module, msg_id, lang_id);
}

USBIP_API std::string format_message(_In_opt_ HMODULE module, _In_ DWORD msg_id, _In_ DWORD lang_id = 0);

} // namespace usbip
