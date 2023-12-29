/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "utils.h"

#include <libusbip/vhci.h>
#include <libusbip/format_message.h>

namespace
{

/*
 * Resource-only DLL that contains RT_MESSAGETABLE
 */
auto get_resource_module() noexcept
{
        static usbip::HModule mod(LoadLibraryEx(L"resources", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR));
        assert(mod);
        return mod.get();
}

} // namespace


wxString usbip::GetLastErrorMsg(DWORD msg_id)
{
        if (msg_id == ~0UL) {
                msg_id = GetLastError();
        }

        auto mod = get_resource_module();
        return wformat_message(mod, msg_id);
}

auto usbip::get_vhci() -> Handle&
{
        static auto handle = vhci::open();
        return handle;
}
