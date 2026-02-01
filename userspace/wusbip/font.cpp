/*
 * Copyright (c) 2024-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "font.h"

#include <wx/frame.h>
#include <wx/window.h>
#include <wx/treelist.h>

namespace
{

using namespace usbip;

auto do_set_font_size(_In_ wxWindow *wnd, _In_ int pt)
{
        auto font = wnd->GetFont();
        font.SetPointSize(pt);
        return wnd->SetFont(font);
}

auto do_change_font_size(_In_ wxWindow *wnd, _In_ int dir)
{
        auto font = wnd->GetFont();
        change_font_size(font, dir);
        return wnd->SetFont(font);
}

} // namespace


void usbip::change_font_size(_Inout_ wxFont &font, _In_ int dir)
{
        wxASSERT(font.IsOk());
        int pt;

        if (!dir) {
                pt = wxNORMAL_FONT->GetPointSize();
        } else if (pt = font.GetPointSize(); dir > 0) {
                 ++pt;
        } else {
                --pt;
        }

        font.SetPointSize(pt);
}

bool usbip::change_font_size(_In_ wxWindow *wnd, _In_ int dir, _In_ bool resursive)
{
        auto ok = do_change_font_size(wnd, dir);
        if (ok && resursive) {
                for (auto child: wnd->GetChildren()) {
                        change_font_size(child, dir, true); // recursion
                }
        }
        return ok;
}

int usbip::get_font_size(_In_ wxWindow *wnd)
{
        auto font = wnd->GetFont();
        return font.GetPointSize();
}

bool usbip::set_font_size(_In_ wxWindow *wnd, _In_ int pt)
{
        auto ok = do_set_font_size(wnd, pt);
        if (ok) {
                for (auto child: wnd->GetChildren()) {
                        set_font_size(child, pt); // recursion
                }
        }
        return ok;
}

bool usbip::change_font_size(_In_ wxTreeListCtrl *tree, _In_ int dir)
{
        return change_font_size(tree, dir, false);
}

bool usbip::set_font_size(_In_ wxTreeListCtrl *tree, _In_ int pt)
{
        return do_set_font_size(tree, pt);
}
