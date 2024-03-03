/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wxutils.h"

#include <wx/window.h>
#include <wx/aui/auibar.h>

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
        auto font = wnd->GetFont();
        auto pt = font.GetPointSize();

        if (!dir) {
                if (auto def_pt = wxNORMAL_FONT->GetPointSize(); pt != def_pt) {
                        pt = def_pt;
                } else {
                        return;
                }
        } else if (dir > 0 && pt < FONT_MAX_POINT) {
                ++pt;
        } else if (dir < 0 && pt > FONT_MIN_POINT) {
                --pt;
        } else {
                return;
        }

        font.SetPointSize(pt);

        if (wnd->SetFont(font)) {
                wnd->Refresh(false);
        } else {
                wxFAIL_MSG("SetFont");
        }
}

bool usbip::set_font_size(_In_ wxWindow *wnd, _In_ int pt)
{
        if (!valid_font_size(pt)) {
                return false;
        }

        auto f = [pt] (auto wnd) 
        {
                if (auto f = wnd->GetFont(); f.GetPointSize() != pt) {
                        f.SetPointSize(pt);
                        if (wnd->SetFont(f)) {
                                wnd->Refresh(false);
                        }
                }
        };

        for_each_child(wnd, f);
        return true;
}

bool usbip::set_font_size(_Inout_ wxAuiToolBar &tb, _In_ int pt)
{
        if (!valid_font_size(pt)) {
                return false;
        }

        auto font = tb.GetFont();
        font.SetPointSize(pt);

        return tb.SetFont(font);
}
