/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <windows.h>
#include <wx/string.h>

#include <compare>
#include <functional>

class wxMenu;
class wxMenuItem;
class wxWindow;

wxMenuItem* clone_menu_item(_In_ wxMenu &dest, _In_ int item_id, _In_ const wxMenu &src);
std::strong_ordering operator <=> (_In_ const wxString &a, _In_ const wxString &b);

namespace usbip
{

void cancel_connect(_In_ HANDLE thread);
void cancel_synchronous_io(_In_ HANDLE thread);

/*
 * @return true if was cancelled 
 */
bool run_cancellable(
        _In_ wxWindow *parent,
        _In_ const wxString &msg,
        _In_ const wxString &caption,
        _In_ std::function<void()> func,
        _In_ const std::function<void(_In_ HANDLE thread)> &cancel = cancel_synchronous_io);

} // namespace usbip

