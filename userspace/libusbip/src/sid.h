/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbip/vhci.h>

#include <string>
#include <expected>
#include <optional>

namespace usbip
{

using vhci::sid_blob;

std::expected<sid_blob, DWORD> get_user_sid() noexcept;

/**
 * Generates SDDL ACEs granting generic all (GA) access to the caller's User SID
 * and, if present, current logon session SID (SE_GROUP_LOGON_ID).
 * @return SDDL string fragment, or Win32 error code on failure
 */
std::expected<std::wstring, DWORD> make_caller_aces(_In_ std::wstring_view ace_flags = L"OICI");

/**
 * @param sid returned by parse_sid* function
 * @return standard SID string notation, ASCII characters only
 */
std::string to_string(_In_ const sid_blob &sid);

/**
 * @param sid returned by parse_sid* function
 * @return standard SID string notation, ASCII characters only
 */
std::wstring to_wstring(_In_ const sid_blob &sid);

/**
 * @param sid returned by parse_sid* function
 * @return hex encoding, '0'-'f' characters only
 * @see parse_sid_hex
 */
std::string to_string_hex(_In_ const sid_blob &sid);

/**
 * Parses a standard SID string notation (e.g. "S-1-5-21-...") or
 * resolves an account name in "domain\\user" or "user" format.
 * @param s standard SID string notation or account name
 * @return binary sid_blob on success, otherwise error code
 * @see to_string_hex
 */
std::expected<sid_blob, DWORD> parse_sid(_In_ const std::string &s);
std::optional<sid_blob> parse_sid_hex(_In_ std::wstring_view hex) noexcept;

} // namespace usbip
