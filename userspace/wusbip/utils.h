/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libusbip/win_handle.h>
#include <usbspec.h>

#include <wx/string.h>

namespace usbip
{

bool init(_Out_ wxString &err);

wxString GetLastErrorMsg(_In_ DWORD msg_id = GetLastError());
Handle& get_vhci();

class UsbIds;
const UsbIds& get_ids();

const char* get_speed_str(_In_ USB_DEVICE_SPEED speed) noexcept;

} // namespace usbip