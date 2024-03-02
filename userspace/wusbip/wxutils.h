/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <functional>

class wxWindow;

namespace usbip
{

void for_each_child(_In_ wxWindow *wnd, _In_ const std::function<void (wxWindow*)> &f);
void change_font_size(_In_ wxWindow *wnd, _In_ int dir);

} // namespace usbip
