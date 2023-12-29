/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/win_handle.h>
#include <wx/string.h>

namespace usbip
{

wxString GetLastErrorMsg(DWORD msg_id = ~0UL);
Handle& get_vhci();

} // namespace usbip