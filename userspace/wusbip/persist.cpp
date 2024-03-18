/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "persist.h"
#include "wusbip.h"
#include "log.h"
#include "font.h"

#include <wx/dataview.h>

namespace
{

using namespace usbip;

auto try_catch(_In_ const char *function, _In_ const std::function<void()> &func)
{
        try {
                func();
        } catch (std::exception &e) {
                wxLogError(_("%s exception: %s"), wxString::FromAscii(function), wxString(e.what(), wxConvLibc));
                return false;
        }

        return true;
}

} // namespace


wxPersistentMainFrame::wxPersistentMainFrame(_In_ MainFrame *frame) : 
        wxPersistentTLW(frame) {}

MainFrame* wxPersistentMainFrame::Get() const 
{ 
        return static_cast<MainFrame*>(wxPersistentTLW::Get()); 
}

void wxPersistentMainFrame::Save() const
{
        wxPersistentTLW::Save();
        auto &frame = *Get();
                
        SaveValue(m_start_in_tray, frame.m_start_in_tray);
        SaveValue(m_close_to_tray, frame.m_close_to_tray);

        if (auto ctl = frame.m_comboBoxServer) {
                SaveValue(m_server, ctl->GetValue());
        }

        if (auto ctl = frame.m_spinCtrlPort) {
                SaveValue(m_port, ctl->GetValue());
        }

        if (auto fr = frame.m_log->GetFrame()) {
                SaveValue(m_show_log_window, fr->IsShown());
        }

        if (auto log = frame.m_log) {
                SaveValue(m_log_verbose, log->GetLogLevel() == VERBOSE_LOGLEVEL);
                SaveValue(m_log_font_size, get_font_size(log));
        }

        if (auto tb = frame.m_auiToolBar) {
                SaveValue(m_toolbar_labels, tb->HasFlag(wxAUI_TB_TEXT));
                SaveValue(m_toolbar_font_size, get_font_size(tb));
        }

        SaveValue(m_toolbar_add_font_size, get_font_size(frame.m_auiToolBarAdd));

        if (auto dv = frame.m_treeListCtrl->GetDataView()) {
                SaveValue(m_tree_row_lines, dv->HasFlag(wxDV_ROW_LINES));
        }

        SaveValue(m_tree_font_size, get_font_size(frame.m_treeListCtrl));
}

bool wxPersistentMainFrame::Restore()
{
        return  wxPersistentTLW::Restore() && 
                try_catch(__func__, [this] { do_restore(); } );
}

void wxPersistentMainFrame::do_restore()
{
        auto &frame = *Get();

        if (bool ok{}; RestoreValue(m_start_in_tray, &ok)) {
                frame.m_start_in_tray = ok;
        }

        if (bool ok{}; RestoreValue(m_close_to_tray, &ok)) {
                frame.m_close_to_tray = ok;
        }

        if (wxString val; RestoreValue(m_server, &val)) {
                frame.m_comboBoxServer->SetValue(val);
        }

        if (int val{}; RestoreValue(m_port, &val)) {
                frame.m_spinCtrlPort->SetValue(val);
        }

        if (bool show{}; RestoreValue(m_show_log_window, &show)) {
                auto fr = frame.m_log->GetFrame();
                fr->Show(show);
        }

        if (bool verbose{}; RestoreValue(m_log_verbose, &verbose)) {
                auto lvl = verbose ? VERBOSE_LOGLEVEL : DEFAULT_LOGLEVEL;
                frame.m_log->SetLogLevel(lvl);
        }

        if (int pt{}; RestoreValue(m_log_font_size, &pt)) {
                set_font_size(frame.m_log, pt);
        }

        if (int pt{}; RestoreValue(m_toolbar_font_size, &pt)) {
                set_font_size(frame.m_auiToolBar, pt);
        }

        if (int pt{}; RestoreValue(m_toolbar_add_font_size, &pt)) {
                set_font_size(frame.m_auiToolBarAdd, pt);
        }

        if (bool ok{}; RestoreValue(m_toolbar_labels, &ok) && ok != frame.m_auiToolBar->HasFlag(wxAUI_TB_TEXT)) {
                wxCommandEvent evt;
                frame.on_view_labels(evt);
        }

        if (bool ok{}; RestoreValue(m_tree_row_lines, &ok) && ok) {
                wxCommandEvent evt;
                frame.on_view_zebra(evt);
        }

        if (int pt{}; RestoreValue(m_tree_font_size, &pt)) {
                set_font_size(frame.m_treeListCtrl, pt);
        }
}
