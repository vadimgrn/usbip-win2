/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */
#include "strconv.h"
#include <windows.h>

std::wstring usbip::utf8_to_wchar(std::string_view str)
{
        std::wstring wstr;
	auto cb = static_cast<int>(str.size());

        auto cch = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(), cb, nullptr, 0);
        if (!cch) {
                return wstr;
        }

        wstr.resize(cch);

        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(), cb, wstr.data(), cch) != cch) {
		wstr.clear();
	}

	return wstr;
}
 
std::string usbip::to_utf8(std::wstring_view wstr)
{
        std::string str;
        auto cch = static_cast<int>(wstr.size());

        auto cb = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wstr.data(), cch, nullptr, 0, nullptr, nullptr);
        if (!cb) {
                return str;
        }

        str.resize(cb);

        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wstr.data(), cch, 
                                str.data(), cb, nullptr, nullptr) != cb) {
                str.clear();
        }

        return str;
}
 
std::wstring usbip::format_message(unsigned int msg_id)
{
        static_assert(sizeof(msg_id) == sizeof(DWORD));

        std::wstring s;
        s.resize(1024);

        if (auto n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, 0, msg_id, 
                                    LANG_USER_DEFAULT, s.data(), DWORD(s.size()), nullptr)) {
                s.resize(n);
        } else {
                auto err = GetLastError();
                s = L"FormatMessage: GetLastError " + std::to_wstring(err);
        }

        return s;
}
