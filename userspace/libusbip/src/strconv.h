/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "..\dllspec.h"

#include <string>
#include <vector>

namespace usbip
{

constexpr auto size_bytes(_In_ std::wstring_view s) noexcept
{
        return s.size()*sizeof(s[0]);
}

USBIP_API std::wstring utf8_to_wchar(_In_ std::string_view s);
USBIP_API std::string wchar_to_utf8(_In_ std::wstring_view ws);

/**
 * Empty strings or strings beginning with L'\0' will be skipped.
 * If string has L'\0', characters after it will not be included.
 * @return multi-sz string
 * Examples: 
 * {"aaa\0\zzz", "\0", "bbb", ""} -> "aaa\0\bbb\0\0"
 * {} -> "\0"
 */
USBIP_API std::wstring make_multi_sz(_In_ const std::vector<std::wstring> &v);

/**
 * @param str list of strings, each ending with L'\0'.
 *            Extra '\0' at end terminates the list.
 *            Split stops if head begins with L'\0'.
 * @return vector of strings that does not contain empty strings
 *
 * Examples:
 * "aaa\0\bbb\0\0" -> {"aaa", "bbb"}
 * "aaa\0\bbb\0\0\ccc" -> {"aaa", "bbb"}
 * "\0aaa\0\bbb\0\0" -> {}
 */
USBIP_API std::vector<std::wstring> split_multi_sz(_In_ std::wstring_view str);

} // namespace usbip
