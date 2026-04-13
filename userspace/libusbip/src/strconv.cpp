/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "strconv.h"

#include <windows.h>

#include <memory>
#include <format>

std::expected<std::wstring, DWORD> usbip::utf8_to_wchar(_In_ std::string_view s)
{
        auto f = [] (const auto &s, auto buf, auto cch) { 
                return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, 
                                           s.data(), static_cast<int>(s.size()), buf, cch); 
        };

        auto cch = f(s, nullptr, 0);
        if (!cch) {
                return std::unexpected(GetLastError());
        }

        std::wstring ws(cch, L'\0');

        if (auto n = f(s, ws.data(), cch); !n) [[unlikely]] {
                return std::unexpected(GetLastError());
        } else if (n != cch) [[unlikely]] {
                ws.resize(n);
        }

	return ws;
}
 
std::expected<std::string, DWORD> usbip::wchar_to_utf8(_In_ std::wstring_view ws)
{
        auto f = [] (const auto &ws, auto buf, auto cb) {
                return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ws.data(), static_cast<int>(ws.size()), 
                                           buf, cb, nullptr, nullptr);
        };

        auto cb = f(ws, nullptr, 0);
        if (!cb) {
                return std::unexpected(GetLastError());
        }

        std::string s(cb, '\0');

        if (auto n = f(ws, s.data(), cb); !n) [[unlikely]] {
                return std::unexpected(GetLastError());
        } else if (n != cb) [[unlikely]] {
                s.resize(n);
        }

        return s;
}

std::wstring usbip::utf8_to_wchar_or_errmsg(_In_ std::string_view s)
{
        auto ws = utf8_to_wchar(s);
        if (!ws) {
                ws = std::format(L"utf8_to_wchar error {}", ws.error());
        }
        return *ws;
}

std::string usbip::wchar_to_utf8_or_errmsg(_In_ std::wstring_view ws)
{
        auto s = wchar_to_utf8(ws);
        if (!s) {
                s = std::format("wchar_to_utf8 error {}", s.error());
        }
        return *s;
}

std::vector<std::wstring> usbip::split_multi_sz(_In_ std::wstring_view str)
{
        std::vector<std::wstring> v;

        while (auto len = wcsnlen_s(str.data(), str.size())) {
                v.emplace_back(str.data(), len);
                str.remove_prefix(std::min(len + 1, str.size())); // skip L'\0'
        }

        return v;
}

std::wstring usbip::make_multi_sz(_In_ const std::vector<std::wstring> &v)
{
        std::wstring str;

        for (auto &s: v) {
                if (auto data = s.data(); auto len = wcsnlen_s(data, s.size())) {
                        str.append(data, len);
                        str += L'\0';
                }
        }

        str += L'\0';
        return str;
}
