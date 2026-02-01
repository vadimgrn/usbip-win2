/*
 * Copyright (c) 2024-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <sal.h>

class wxFont;
class wxWindow;
class wxTreeListCtrl;

namespace usbip
{

void change_font_size(_Inout_ wxFont &font, _In_ int dir);
bool change_font_size(_In_ wxWindow *wnd, _In_ int dir, _In_ bool resursive = true);

bool set_font_size(_In_ wxWindow *wnd, _In_ int pt);
int get_font_size(_In_ wxWindow *wnd);

bool change_font_size(_In_ wxTreeListCtrl *tree, _In_ int dir);
bool set_font_size(_In_ wxTreeListCtrl *tree, _In_ int pt);

} // namespace usbip
