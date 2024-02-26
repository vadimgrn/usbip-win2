/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "wusbip.h"

#include <wx/aui/auibar.h>
#include <wx/persist/toplevel.h>

class wxPersistentMainFrame : public wxPersistentTLW
{
public:
        explicit wxPersistentMainFrame(_In_ MainFrame *frame) : 
                wxPersistentTLW(frame) {}

private:
        static inline auto &m_server = L"Server";
        static inline auto &m_port = L"Port";
        static inline auto &m_loglevel = L"LogLevel";
        static inline auto &m_toolbar_labels = L"ToolBarLabels";
        static inline auto &m_show_log_window = L"ShowLogWindow";

        void Save() const override;
        bool Restore() override;

        auto Get() const { return static_cast<MainFrame*>(wxPersistentTLW::Get()); }
        bool do_restore();
};

inline auto wxCreatePersistentObject(_In_ MainFrame *frame)
{
        return new wxPersistentMainFrame(frame);
}


class wxPersistentAuiToolBar : public wxPersistentWindow<wxAuiToolBar>
{
public:
        explicit wxPersistentAuiToolBar(_In_ wxAuiToolBar *tb) : 
                wxPersistentWindow(tb) {}

        wxString GetKind() const override { return L"AuiToolBar"; }

private:
        void Save() const override;
        bool Restore() override;

        bool do_restore();
};

inline auto wxCreatePersistentObject(_In_ wxAuiToolBar *tb)
{
        return new wxPersistentAuiToolBar(tb);
}
