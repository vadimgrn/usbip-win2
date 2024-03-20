/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wx/persist/toplevel.h>

class MainFrame;

class wxPersistentMainFrame : public wxPersistentTLW
{
public:
        explicit wxPersistentMainFrame(_In_ MainFrame *frame);

private:
        static inline auto &m_server = L"Server";
        static inline auto &m_port = L"Port";
        static inline auto &m_log_verbose = L"LogVerbose";
        static inline auto &m_toolbar_labels = L"ToolBarLabels";
        static inline auto &m_show_log_window = L"ShowLogWindow";
        static inline auto &m_log_font_size = L"LogFontSize";
        static inline auto &m_tree_font_size = L"TreeFontSize";
        static inline auto &m_tree_row_lines = L"TreeRowLines";
        static inline auto &m_start_in_tray = L"StartInTray";
        static inline auto &m_close_to_tray = L"CloseToTray";

        void Save() const override;
        bool Restore() override;

        MainFrame* Get() const; // hides wxPersistentWindow::Get()
        void do_restore();
};

inline auto wxCreatePersistentObject(_In_ MainFrame *frame)
{
        return new wxPersistentMainFrame(frame);
}
