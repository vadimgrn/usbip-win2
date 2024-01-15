/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "utils.h"
#include "resource.h"

#include <libusbip/vhci.h>
#include <libusbip/remote.h>
#include <libusbip/win_socket.h>
#include <libusbip/format_message.h>
#include <libusbip/src/usb_ids.h>
#include <libusbip/src/file_ver.h>

#include <wx/translation.h>
#include <format>

namespace
{

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

/*
 * @see GetModuleFileName
 */
auto get_module_filename(_Out_ wxString &path)
{
        char *val{};
        auto err = _get_pgmptr(&val); // wxWidgets uses WinMain
        if (!err) {
                path = wxString(val, wxConvLocal); // FIXME: what encoding of val?
        }
        return err;
}

} // namespace


auto win::get_file_version(_In_ std::wstring_view path) -> const FileVersion&
{
        static FileVersion v(path);
        wxASSERT(v);
        return v;
}

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

        if (wxString path; auto errc = get_module_filename(path)) {
                err = wxString::Format(_("_get_pgmptr error %d\n%s"), errc, _wcserror(errc));
                return false;
        } else {
                auto sv = wstring_view(path);
                win::get_file_version(sv); // must be the first call
        }

        static InitWinSock2 ws2;
        if (!ws2) {
                err = wxString::Format(_("WSAStartup error\n%s"), GetLastErrorMsg());
                return false;
        }

        return static_cast<bool>(get_vhci());
}

const wchar_t* usbip::get_speed_str(_In_ USB_DEVICE_SPEED speed) noexcept
{
        static_assert(UsbLowSpeed == 0);
        static_assert(UsbFullSpeed == 1);
        static_assert(UsbHighSpeed == 2);
        static_assert(UsbSuperSpeed == 3);

        const wchar_t *str[] { L"Low", L"Full", L"High", L"Super" };
        return speed >= 0 && speed < ARRAYSIZE(str) ? str[speed] : L"?";
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

wxString usbip::make_server_url(_In_ const device_location &loc)
{
        auto url = std::format("{}:{}", loc.hostname, loc.service);
        return wxString::FromUTF8(url);
}

wxString usbip::make_server_url(_In_ const wxString &hostname, _In_ const wxString &service)
{
        return hostname + L':' + service;
}
