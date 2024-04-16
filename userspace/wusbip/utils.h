/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/vhci.h>

#include <string_view>
#include <tuple>

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
bool get_speed_val(_Out_ USB_DEVICE_SPEED &val, _In_ const wxString &speed) noexcept;

struct usb_device;
imported_device make_imported_device(_In_ std::string hostname, _In_ std::string service, _In_ const usb_device &dev);

wxString make_device_url(_In_ const device_location &loc);

wxString make_server_url(_In_ const device_location &loc);
wxString make_server_url(_In_ const wxString &hostname, _In_ const wxString &service);

bool split_server_url(_In_ const wxString &url, _Out_ wxString &hostname, _Out_ wxString &service);

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

constexpr auto tie(_In_ const device_location &loc)
{
        return std::tie(loc.hostname, loc.service, loc.busid); // tuple of lvalue references
}

constexpr auto operator == (_In_ const device_location &a, _In_ const device_location &b)
{
        return tie(a) == tie(b);
}

constexpr auto operator <=> (_In_ const device_location &a, _In_ const device_location &b)
{
        return tie(a) <=> tie(b);
}

} // namespace usbip
