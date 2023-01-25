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

        auto errmsg = [] {
                auto err = GetLastError();
                return L"MultiByteToWideChar: GetLastError " + std::to_wstring(err);
        };

        auto f = [] (auto &s, auto buf, auto cch) { 
                return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, 
                                           s.data(), static_cast<int>(s.size()), buf, cch); 
        };

        auto cch = f(s, nullptr, 0);
        if (!cch) {
                return ws = errmsg();
        }

        ws.resize(cch);

        if (f(s, ws.data(), cch) != cch) {
		ws = errmsg();
	}

	return ws;
}
 
std::string usbip::wchar_to_utf8(std::wstring_view ws)
{
        std::string s;

        auto errmsg = [] {
                auto err = GetLastError();
                return "WideCharToMultiByte: GetLastError " + std::to_string(err);
        };

        auto f = [] (auto &s, auto buf, auto cb) {
                return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), 
                                           buf, cb, nullptr, nullptr);
        };

        auto cb = f(ws, nullptr, 0);
        if (!cb) {
                return s = errmsg();
        }

        s.resize(cb);

        if (f(ws, s.data(), cb) != cb) {
                s = errmsg();
        }

        return s;
}

std::wstring usbip::wformat_message(unsigned long msg_id)
{
        static_assert(sizeof(msg_id) == sizeof(DWORD));
        std::wstring msg;

        const auto flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | 
                           FORMAT_MESSAGE_IGNORE_INSERTS;

        std::unique_ptr<void, decltype(LocalFree)&> buf_ptr(nullptr, LocalFree);
        LPWSTR buf{};

        if (auto cch = FormatMessageW(flags, nullptr, msg_id, 0, (LPWSTR)&buf, 0, nullptr)) {
                buf_ptr.reset(buf);
                msg.assign(buf, cch);
        } else {
                auto err = GetLastError();
                msg = L"FormatMessageW: GetLastError " + std::to_wstring(err);
        }

        return msg;
}
