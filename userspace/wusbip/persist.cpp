/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "persist.h"
#include "wusbip.h"
#include "log.h"

namespace
{

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
                auto verbose = log->GetLogLevel() == VERBOSE_LOGLEVEL;
                SaveValue(m_log_verbose, verbose);
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
        return  wxPersistentTLW::Restore() && 
                try_catch(__func__, [this] { do_restore(); } );
}

void wxPersistentMainFrame::do_restore()
{
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

        if (bool verbose{}; RestoreValue(m_log_verbose, &verbose)) {
                auto lvl = verbose ? VERBOSE_LOGLEVEL : DEFAULT_LOGLEVEL;
                frame.m_log->SetLogLevel(lvl);
        }

        if (auto item = frame.m_menu_view->FindItem(frame.ID_VIEW_LABELS); !item) {
                wxASSERT(!"ID_VIEW_LABELS not found");
        } else if (bool check{}; RestoreValue(m_toolbar_labels, &check) && check != item->IsChecked()) {
                item->Toggle();
                wxCommandEvent evt;
                frame.on_view_labels(evt);
        }
}
