/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <functional>

class wxWindow;
class wxAuiToolBar;

namespace usbip
{

enum { FONT_MIN_POINT = 6, FONT_MAX_POINT = 72 };

constexpr auto valid_font_size(_In_ int pt)
{
        return pt >= FONT_MIN_POINT && pt <= FONT_MAX_POINT;
}

void for_each_child(_In_ wxWindow *wnd, _In_ const std::function<void (wxWindow*)> &f);
void change_font_size(_In_ wxWindow *wnd, _In_ int dir);

bool set_font_size(_In_ wxWindow *wnd, _In_ int pt);
bool set_font_size(_Inout_ wxAuiToolBar &tb, _In_ int pt);

} // namespace usbip
