/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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

inline auto what(_In_ const std::exception &e)
{
        return wxString(e.what(), wxConvLibc);
}

using cancel_function = decltype(CancelSynchronousIo);
#ifdef _WIN64
cancel_function cancel_connect;
#else
BOOL cancel_connect(_In_ HANDLE thread);
#endif

void run_cancellable(
        _In_ wxWindow *parent,
        _In_ const wxString &msg,
        _In_ const wxString &caption,
        _In_ std::function<void()> func,
        _In_ const std::function<cancel_function> &cancel = CancelSynchronousIo);

} // namespace usbip

