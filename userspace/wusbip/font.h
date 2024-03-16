/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <sal.h>

class wxWindow;
class wxLogWindow;
class wxTreeListCtrl;

namespace usbip
{

enum { FONT_MIN_POINT = 6, FONT_MAX_POINT = 36 };

constexpr auto valid_font_size(_In_ int pt)
{
        return pt >= FONT_MIN_POINT && pt <= FONT_MAX_POINT;
}

bool change_font_size(_In_ wxWindow *wnd, _In_ int dir);

bool set_font_size(_In_ wxWindow *wnd, _In_ int pt);
int get_font_size(_In_ wxWindow *wnd);

bool set_font_size(_In_ wxLogWindow *wnd, _In_ int pt);
int get_font_size(_In_ wxLogWindow *wnd);

bool set_font_size(_In_ wxTreeListCtrl *tree, _In_ int pt);
void make_header_bold(_In_ wxTreeListCtrl *tree);

} // namespace usbip
