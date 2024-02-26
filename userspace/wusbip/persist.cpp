/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "persist.h"

void wxPersistentMainFrame::Save() const
{
        wxPersistentTLW::Save();
        auto &frame = *Get();
                
        if (auto ctl = frame.m_textCtrlServer) {
                SaveValue(m_server, ctl->GetValue());
        }

        if (auto ctl = frame.m_spinCtrlPort) {
                SaveValue(m_port, ctl->GetValue());
        }

        if (auto fr = frame.m_log->GetFrame()) {
                SaveValue(m_show_log_window, fr->IsShown());
        }

        if (auto log = frame.m_log) {
                auto lvl = log->GetLogLevel();
                static_assert(sizeof(lvl) == sizeof(long));
                SaveValue(m_loglevel, static_cast<long>(lvl));
        }

        if (auto item = frame.m_menu_view->FindItem(frame.ID_VIEW_LABELS)) {
                wxASSERT(item->IsCheck());
                SaveValue(m_toolbar_labels, item->IsChecked());
        } else {
                wxASSERT(!"ID_VIEW_LABELS not found");
        }
}

bool wxPersistentMainFrame::Restore()
{
        try {
                return do_restore();
        } catch (std::exception &e) {
                wxLogError(_("%s exception: %s"), wxString::FromAscii(__func__), wxString(e.what(), wxConvLibc));
                return false;
        }
}

bool wxPersistentMainFrame::do_restore()
{
        if (!wxPersistentTLW::Restore()) {
                return false;
        }

        auto &frame = *Get();
                
        if (wxString val; RestoreValue(m_server, &val)) {
                frame.m_textCtrlServer->SetValue(val);
        }

        if (int val{}; RestoreValue(m_port, &val)) {
                frame.m_spinCtrlPort->SetValue(val);
        }

        if (bool show{}; RestoreValue(m_show_log_window, &show)) {
                auto fr = frame.m_log->GetFrame();
                fr->Show(show);
        }

        if (long lvl{}; RestoreValue(m_loglevel, &lvl)) {
                static_assert(sizeof(lvl) == sizeof(wxLogLevel));
                frame.m_log->SetLogLevel(static_cast<wxLogLevel>(lvl));
        }

        if (auto item = frame.m_menu_view->FindItem(frame.ID_VIEW_LABELS); !item) {
                wxASSERT(!"ID_VIEW_LABELS not found");
        } else if (bool check{}; RestoreValue(m_toolbar_labels, &check) && check != item->IsChecked()) {
                item->Toggle();
                wxCommandEvent evt;
                frame.on_view_labels(evt);
        }

        return true;
}

void wxPersistentAuiToolBar::Save() const 
{
}

bool wxPersistentAuiToolBar::Restore() 
{ 
        try {
                return do_restore();
        } catch (std::exception &e) {
                wxLogError(_("%s exception: %s"), wxString::FromAscii(__func__), wxString(e.what(), wxConvLibc));
                return false;
        }
}

bool wxPersistentAuiToolBar::do_restore() 
{ 
        return true;
}
