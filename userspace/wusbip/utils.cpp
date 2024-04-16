/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "utils.h"
#include "resource.h"

#include <libusbip/remote.h>
#include <libusbip/win_socket.h>
#include <libusbip/format_message.h>
#include <libusbip/src/usb_ids.h>
#include <libusbip/src/file_ver.h>
#include <libusbip/output.h>

#include <wx/log.h>
#include <wx/translation.h>

namespace
{

static_assert(UsbLowSpeed == 0);
static_assert(UsbFullSpeed == 1);
static_assert(UsbHighSpeed == 2);
static_assert(UsbSuperSpeed == 3);

const wchar_t *g_usb_speed_str[] { L"Low", L"Full", L"High", L"Super" }; // indexed by enum USB_DEVICE_SPEED

auto &msgtable_dll = L"resources"; // resource-only DLL that contains RT_MESSAGETABLE

auto& get_resource_module() noexcept
{
        static usbip::HModule mod(LoadLibraryEx(msgtable_dll, nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR));
        return mod;
}

auto get_ids_data()
{
        win::Resource r(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_USB_IDS), RT_RCDATA);
        wxASSERT(r);
        return r.str();
}

} // namespace


auto win::get_file_version() -> const FileVersion&
{
        static FileVersion v;
        wxASSERT(v);
        return v;
}

/*
 * Use for libusbip.dll errors only. 
 * @see wxSysErrorMsg
 */
wxString usbip::GetLastErrorMsg(_In_ DWORD msg_id)
{
        auto &mod = get_resource_module();
        return wformat_message(mod.get(), msg_id);
}

auto usbip::get_vhci() -> Handle&
{
        static auto handle = vhci::open();
        return handle;
}

auto usbip::get_ids() -> const UsbIds&
{
        static UsbIds ids(get_ids_data());
        wxASSERT(ids);
        return ids;
}

bool usbip::init(_Out_ wxString &err)
{
        wxASSERT(err.empty());

        if (!get_resource_module()) {
                auto ec = GetLastError();
                err = wxString::Format(_("Cannot load '%s.dll'\nError %lu\n%s"), msgtable_dll, err, wformat_message(ec));
                return false;
        }

        static InitWinSock2 ws2;
        if (!ws2) {
                auto ec = GetLastError();
                err = wxString::Format(_("WSAStartup error %lu\n%s"), ec, wxSysErrorMsg(ec));
                return false;
        }

        auto logger = [] (auto s) 
        { 
                wxLogVerbose(wxString::FromUTF8(s)); 
        };
        libusbip::set_debug_output(logger);

        return static_cast<bool>(get_vhci());
}

const wchar_t* usbip::get_speed_str(_In_ USB_DEVICE_SPEED speed) noexcept
{
        return speed >= 0 && speed < ARRAYSIZE(g_usb_speed_str) ? g_usb_speed_str[speed] : L"?";
}

bool usbip::get_speed_val(_Out_ USB_DEVICE_SPEED &val, _In_ const wxString &speed) noexcept
{
        for (int i = 0; i < ARRAYSIZE(g_usb_speed_str); ++i) {
                if (speed.IsSameAs(g_usb_speed_str[i], false)) {
                        val = static_cast<USB_DEVICE_SPEED>(i);
                        return true;
                }
        }

        return false;
}

auto usbip::make_imported_device(
        _In_ std::string hostname, _In_ std::string service, _In_ const usb_device &dev) -> imported_device
{
        return imported_device {
                .location = {
                        .hostname = std::move(hostname), 
                        .service = std::move(service), 
                        .busid = dev.busid 
                },

                .devid = make_devid(dev.busnum, dev.devnum),
                .speed = dev.speed,

                .vendor = dev.idVendor,
                .product = dev.idProduct 
        };
}

wxString usbip::make_device_url(_In_ const device_location &loc)
{
        auto url = loc.hostname + ':' + loc.service + '/' + loc.busid;
        return wxString::FromUTF8(url);
}

wxString usbip::make_server_url(_In_ const device_location &loc)
{
        auto url = loc.hostname + ':' + loc.service;
        return wxString::FromUTF8(url);
}

wxString usbip::make_server_url(_In_ const wxString &hostname, _In_ const wxString &service)
{
        return hostname + L':' + service;
}

bool usbip::split_server_url(_In_ const wxString &url, _Out_ wxString &hostname, _Out_ wxString &service)
{
        auto pos = url.find_last_of(L':');
        auto found = pos != url.npos;
        
        if (found) {
                hostname = url.substr(0, pos);
                service = url.substr(++pos);
        }

        return found;
}
