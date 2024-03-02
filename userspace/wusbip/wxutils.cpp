/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wxutils.h"
#include <wx/window.h>

void usbip::for_each_child(_In_ wxWindow *wnd, _In_ const std::function<void (wxWindow*)> &f)
{
        auto &lst = wnd->GetChildren();

        for (auto node = lst.GetFirst(); node; node = node->GetNext()) {
                auto child = static_cast<wxWindow*>(node->GetData());
                f(child);
        }
}

/*
 * auto f = dir > 0 ? &wxFont::MakeLarger : &wxFont::MakeSmaller;
 * (font.*f)();
 */
void usbip::change_font_size(_In_ wxWindow *wnd, _In_ int dir)
{
        enum { MIN_POINT = 6, MAX_POINT = 72 };

        auto font = wnd->GetFont();
        auto pt = font.GetPointSize();

        if (!dir) {
                if (auto def_pt = wxNORMAL_FONT->GetPointSize(); pt != def_pt) {
                        pt = def_pt;
                } else {
                        return;
                }
        } else if (dir > 0 && pt < MAX_POINT) {
                ++pt;
        } else if (dir < 0 && pt > MIN_POINT) {
                --pt;
        } else {
                return;
        }

        font.SetPointSize(pt);
        wnd->SetFont(font);

        wnd->Refresh(false);
}
