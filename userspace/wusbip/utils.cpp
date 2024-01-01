/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "utils.h"
#include "resource.h"

#include <libusbip/vhci.h>
#include <libusbip/win_socket.h>
#include <libusbip/format_message.h>
#include <libusbip/src/usb_ids.h>

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
        assert(r);
        return r.str();
}

} // namespace


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
        assert(ids);
        return ids;
}

bool usbip::init(_Out_ wxString &err)
{
        assert(err.empty());

        if (!get_resource_module()) {
                auto ec = GetLastError();
                err = wxString::Format("Can't load '%s.dll', error %#x %s", msgtable_dll, err, wformat_message(ec));
                return false;
        }

        static InitWinSock2 ws2;
        if (!ws2) {
                err = wxString::Format("Can't initialize Windows Sockets 2, %s", GetLastErrorMsg());
                return false;
        }

        return static_cast<bool>(get_vhci());
}