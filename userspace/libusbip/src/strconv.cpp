/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */
#include "strconv.h"

#include <windows.h>

#include <cassert>
#include <memory>
#include <format>

std::wstring usbip::utf8_to_wchar(_In_ std::string_view s)
{
        std::wstring ws;

        auto f = [] (auto &s, auto buf, auto cch) { 
                return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, 
                                           s.data(), static_cast<int>(s.size()), buf, cch); 
        };

        auto cch = f(s, nullptr, 0);
        if (!cch) {
                auto err = GetLastError();
                return L"MultiByteToWideChar error " + std::format(L"{:#x}", err);
        }

        ws.resize(cch);

        if (auto n = f(s, ws.data(), cch); n != cch) [[unlikely]] {
                ws.resize(n);
                assert(!"MultiByteToWideChar");
        }

	return ws;
}
 
std::string usbip::wchar_to_utf8(_In_ std::wstring_view ws)
{
        std::string s;

        auto f = [] (auto &ws, auto buf, auto cb) {
                return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ws.data(), static_cast<int>(ws.size()), 
                                           buf, cb, nullptr, nullptr);
        };

        auto cb = f(ws, nullptr, 0);
        if (!cb) {
                auto err = GetLastError();
                return "WideCharToMultiByte error " + std::format("{:#x}", err);
        }

        s.resize(cb);

        if (auto n = f(ws, s.data(), cb); n != cb) [[unlikely]] {
                s.resize(n);
                assert(!"WideCharToMultiByte");
        }

        return s;
}

std::vector<std::wstring> usbip::split_multi_sz(_In_ std::wstring_view str)
{
        std::vector<std::wstring> v;

        while (!str.empty() && str.front()) {
                auto str_sz = str.size();

                auto len = wcsnlen_s(str.data(), str_sz);
                v.emplace_back(str.data(), len);

                str.remove_prefix(min(len + 1, str_sz)); // skip L'\0'
        }

        return v;
}

std::wstring usbip::make_multi_sz(_In_ const std::vector<std::wstring> &v)
{
        std::wstring str;

        for (auto &s: v) {
                str += s;
                str += L'\0';
        }

        str += L'\0';
        return str;
}
