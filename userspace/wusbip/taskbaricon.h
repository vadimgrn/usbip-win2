/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wx/taskbar.h>
#include <memory>

class wxMenu;
class MainFrame;

class TaskBarIcon : public wxTaskBarIcon
{
public:
        TaskBarIcon();

private:
        std::unique_ptr<wxMenu> m_popup;

        wxMenu *GetPopupMenu() override;
        std::unique_ptr<wxMenu> create_popup_menu();
        MainFrame& frame() const;

        void on_left_dclick(wxTaskBarIconEvent&);
        void on_open(wxCommandEvent&);
};
