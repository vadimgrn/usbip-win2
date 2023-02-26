/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <string>
#include <vector>

namespace usbip
{

std::wstring utf8_to_wchar(_In_ std::string_view s);
std::string wchar_to_utf8(_In_ std::wstring_view ws);

/*
 * @param str list of strings, each ending with L'\0'.
 *            Extra '\0' at end terminates the list.
 *            Split stops if head begins with L'\0'.
 * @return vector of strings that does not contain empty strings
 *
 * Examples:
 * "aaa\0\bbb\0" -> {"aaa", "bbb"}
 * "aaa\0\bbb\0\0\ccc" -> {"aaa", "bbb"}
 * "\0aaa\0\bbb\0" -> {}
 */
std::vector<std::wstring> split_multisz(_In_ std::wstring_view str);

} // namespace usbip
