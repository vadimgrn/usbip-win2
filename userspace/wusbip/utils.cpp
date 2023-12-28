/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "utils.h"

#include <libusbip/vhci.h>
#include <libusbip/format_message.h>

namespace
{

auto &msgtable_dll = L"resources"; // resource-only DLL that contains RT_MESSAGETABLE

auto& get_resource_module() noexcept
{
        static usbip::HModule mod(LoadLibraryEx(msgtable_dll, nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR));
        return mod;
}

} // namespace


std::string usbip::GetLastErrorMsg(unsigned long msg_id)
{
        static_assert(sizeof(msg_id) == sizeof(UINT32));
        static_assert(std::is_same_v<decltype(msg_id), DWORD>);

        if (msg_id == ~0UL) {
                msg_id = GetLastError();
        }

        auto &mod = get_resource_module();
        return format_message(mod.get(), msg_id);
}

auto usbip::get_vhci() -> Handle&
{
        static auto handle = vhci::open(true);
        return handle;
}

