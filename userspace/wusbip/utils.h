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
const FileVersion& get_file_version();

} // namespace win


namespace usbip
{

bool init(_Out_ wxString &err);

wxString GetLastErrorMsg(_In_ DWORD msg_id = GetLastError());
Handle& get_vhci();

class UsbIds;
const UsbIds& get_ids();

const wchar_t* get_speed_str(_In_ USB_DEVICE_SPEED speed) noexcept;

struct usb_device;
struct imported_device;
imported_device make_imported_device(_In_ std::string hostname, _In_ std::string service, _In_ const usb_device &dev);

struct device_location;
wxString make_server_url(_In_ const device_location &loc);

wxString make_server_url(_In_ const wxString &hostname, _In_ const wxString &service);

constexpr UINT32 make_devid(_In_ int busnum, _In_ int devnum) noexcept
{
        return (busnum << 16) | (devnum & USHRT_MAX);
}

inline auto wstring_view(_In_ const wxString &s) noexcept
{
        return std::wstring_view(s.wx_str(), s.length());
}

inline auto wx_string(_In_ std::wstring_view s)
{
        return wxString(s.data(), s.length());
}

} // namespace usbip