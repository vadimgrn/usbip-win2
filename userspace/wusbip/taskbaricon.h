/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wx/taskbar.h>
#include <memory>

class wxMenu;
class wxWindow;

class TaskBarIcon : public wxTaskBarIcon
{
public:
        TaskBarIcon();

private:
        std::unique_ptr<wxMenu> m_popup;

        wxMenu *GetPopupMenu() override;
        std::unique_ptr<wxMenu> create_popup_menu();

        wxWindow& get_main_window() const;

        void on_exit(wxCommandEvent&);
        void on_left_dclick(wxTaskBarIconEvent&);
        void on_show_update_ui(wxUpdateUIEvent &event);
        void on_show(wxCommandEvent&);
};
