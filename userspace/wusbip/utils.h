/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/win_handle.h>
#include <usbspec.h>

#include <string_view>
#include <wx/string.h>

namespace win
{

class FileVersion;
const FileVersion& get_file_version(_In_ std::wstring_view path = L"");

} // namespace win


namespace usbip
{

bool init(_Out_ wxString &err);

wxString GetLastErrorMsg(_In_ DWORD msg_id = GetLastError());
Handle& get_vhci();

class UsbIds;
const UsbIds& get_ids();

const char* get_speed_str(_In_ USB_DEVICE_SPEED speed) noexcept;

inline auto wstring_view(_In_ const wxString &s) noexcept
{
        return std::wstring_view(s.wx_str(), s.length());
}

inline auto wx_string(_In_ std::wstring_view s)
{
        return wxString(s.data(), s.length());
}

} // namespace usbip