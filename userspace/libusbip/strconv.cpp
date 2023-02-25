/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */
#include "strconv.h"

#include <cassert>
#include <memory>

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
                return L"MultiByteToWideChar: GetLastError " + std::to_wstring(err);
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
                return "WideCharToMultiByte: GetLastError " + std::to_string(err);
        }

        s.resize(cb);

        if (auto n = f(ws, s.data(), cb); n != cb) [[unlikely]] {
                s.resize(n);
                assert(!"WideCharToMultiByte");
        }

        return s;
}

std::wstring usbip::wformat_message(
        _In_ DWORD flags, _In_opt_ HMODULE module, _In_ DWORD msg_id, _In_ DWORD lang_id)
{
        std::wstring msg;

        flags |= FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | 
                 FORMAT_MESSAGE_MAX_WIDTH_MASK; // do not append '\n'

        if (LPWSTR buf{}; auto cch = FormatMessageW(flags, module, msg_id, lang_id, (LPWSTR)&buf, 0, nullptr)) {
                std::unique_ptr<void, decltype(LocalFree)&> buf_ptr(buf, LocalFree);
                msg.assign(buf, cch);
        } else {
                auto err = GetLastError();
                msg = L"FormatMessageW: GetLastError " + std::to_wstring(err);
        }

        return msg;
}

std::vector<std::wstring> usbip::split_multiz(_In_ std::wstring_view str)
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
