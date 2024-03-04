/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "font.h"

#include <wx/log.h>
#include <wx/frame.h>
#include <wx/window.h>
#include <wx/treelist.h>

#include <functional>

namespace
{

using namespace usbip;

void for_each_child(_In_ wxWindow *wnd, _In_ const std::function<void (wxWindow*)> &f)
{
        wxASSERT(wnd);
        auto &lst = wnd->GetChildren();

        for (auto node = lst.GetFirst(); node; node = node->GetNext()) {
                auto child = static_cast<wxWindow*>(node->GetData());
                f(child);
        }
}

auto do_set_font_size(_In_ wxWindow *wnd, _In_ int pt)
{
        if (auto font = wnd->GetFont(); !font.IsOk()) {
                return false;
        } else if (font.GetPointSize() == pt) {
                return true;
        } else {
                font.SetPointSize(pt);
                return wnd->SetFont(font);
        }
}

/*
 * auto f = dir > 0 ? &wxFont::MakeLarger : &wxFont::MakeSmaller;
 * (font.*f)();
 */
auto do_change_font_size(_In_ wxWindow *wnd, _In_ int dir)
{
        auto font = wnd->GetFont();
        if (!font.IsOk()) {
                return false;
        }

        auto pt = font.GetPointSize();

        if (!dir) {
                if (auto def_pt = wxNORMAL_FONT->GetPointSize(); pt != def_pt) {
                        pt = def_pt;
                } else {
                        return true;
                }
        } else if (dir > 0 && pt < FONT_MAX_POINT) {
                ++pt;
        } else if (dir < 0 && pt > FONT_MIN_POINT) {
                --pt;
        } else {
                return false;
        }

        font.SetPointSize(pt);
        return wnd->SetFont(font);
}

bool change_font_size(_In_ wxWindow *wnd, _In_ int dir)
{
        auto ok = do_change_font_size(wnd, dir);
        if (ok) {
                auto f = [dir] (auto wnd) { change_font_size(wnd, dir); }; // recursion
                for_each_child(wnd, f);
        }
        return ok;
}

} // namespace


bool usbip::change_font_size(_In_ wxWindow *wnd, _In_ int dir)
{
        auto ok = ::change_font_size(wnd, dir);
        if (ok) {
                wnd->Refresh(false);
        }
        return ok;
}

bool usbip::set_font_size(_In_ wxWindow *wnd, _In_ int pt)
{
        auto ok = valid_font_size(pt) && do_set_font_size(wnd, pt);
        if (ok) {
                auto f = [pt] (auto wnd) { set_font_size(wnd, pt); }; // recursion
                for_each_child(wnd, f);
        }
        return ok;
}

int usbip::get_font_size(_In_ wxWindow *wnd)
{
        auto font = wnd->GetFont();
        return font.GetPointSize();
}

bool usbip::set_font_size(_In_ wxLogWindow *wnd, _In_ int pt)
{
        return set_font_size(wnd->GetFrame(), pt);
}

int usbip::get_font_size(_In_ wxLogWindow *log)
{
        return get_font_size(log->GetFrame());
}

bool usbip::set_font_size(_In_ wxTreeListCtrl *tree, _In_ int pt)
{
        return set_font_size(tree->GetView(), pt);
}

int usbip::get_font_size(_In_ wxTreeListCtrl *tree)
{
        return get_font_size(tree->GetView());
}
