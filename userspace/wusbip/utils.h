/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/win_handle.h>
#include <wx/string.h>

namespace usbip
{

bool init(_Out_ wxString &err);

wxString GetLastErrorMsg(_In_ DWORD msg_id = GetLastError());
Handle& get_vhci();

class UsbIds;
const UsbIds& get_ids();

} // namespace usbip