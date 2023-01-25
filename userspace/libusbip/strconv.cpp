/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */
#include "strconv.h"
#include <windows.h>

#include <memory>

namespace
{

/*
 * #include <system_error>
 * std::system_category().message(ERROR_INVALID_PARAMETER);
 */
template<typename R, typename STR, typename FMT, typename TO_STR>
inline auto format_message(FMT format, TO_STR to_str, STR errm, DWORD msg_id)
{
        R msg;

        std::unique_ptr<void, decltype(LocalFree)&> buf_ptr(nullptr, LocalFree);
        STR buf;

        const auto flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;

        if (auto n = format(flags, nullptr, msg_id, 0, (STR)&buf, 0, nullptr)) {
                buf_ptr.reset(buf);
                msg.assign(buf, n);
        } else {
                auto err = GetLastError();
                msg = errm + to_str(err);
        }

        return msg;
}

} // namespace


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
 
std::string usbip::wchar_to_utf8(std::wstring_view wstr)
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

std::string usbip::format_message(unsigned long msg_id)
{
        static_assert(sizeof(msg_id) == sizeof(DWORD));

        using func = std::string(DWORD);
        func &to_str = std::to_string;

        auto errm = (LPSTR)"FormatMessageA: GetLastError ";
        return ::format_message<std::string, LPSTR>(FormatMessageA, to_str, errm, msg_id);
}

std::wstring usbip::wformat_message(unsigned long msg_id)
{
        using func = std::wstring(DWORD);
        func &to_str = std::to_wstring;

        auto errm = (LPWSTR)L"FormatMessageW: GetLastError ";
        return ::format_message<std::wstring, LPWSTR>(FormatMessageW, to_str, errm, msg_id);
}
