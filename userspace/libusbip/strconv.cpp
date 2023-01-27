/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */
#include "strconv.h"

#include <cassert>
#include <memory>

#include <windows.h>

std::wstring usbip::utf8_to_wchar(std::string_view s)
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
 
std::string usbip::wchar_to_utf8(std::wstring_view ws)
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

std::wstring usbip::wformat_message(unsigned long msg_id)
{
        static_assert(sizeof(msg_id) == sizeof(DWORD));
        std::wstring msg;

        const auto flags = FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_ALLOCATE_BUFFER |
                           FORMAT_MESSAGE_IGNORE_INSERTS;

        if (LPWSTR buf{}; auto cch = FormatMessageW(flags, nullptr, msg_id, 0, (LPWSTR)&buf, 0, nullptr)) {
                std::unique_ptr<void, decltype(LocalFree)&> buf_ptr(buf, LocalFree);
                msg.assign(buf, cch);
                rtrim(msg);
        } else {
                auto err = GetLastError();
                msg = L"FormatMessageW: GetLastError " + std::to_wstring(err);
        }

        return msg;
}
