/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */
#include "..\format_message.h"
#include "strconv.h"

#include <memory>
#include <format>

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
                msg = std::format(L"FormatMessageW error {:#x}", GetLastError());
        }

        return msg;
}

std::string usbip::format_message(_In_ DWORD msg_id, _In_ DWORD lang_id) 
{ 
        auto ws = wformat_message(msg_id, lang_id);
        return wchar_to_utf8_or_errmsg(ws);
}

std::string usbip::format_message(_In_opt_ HMODULE module, _In_ DWORD msg_id, _In_ DWORD lang_id)
{
        auto ws = wformat_message(module, msg_id, lang_id);
        return wchar_to_utf8_or_errmsg(ws);
}
