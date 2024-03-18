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

        template<typename... Args>
        auto show_baloon(Args&& ...args)
        {
                return ShowBalloon(std::forward<Args>(args)...);
        }

private:
        std::unique_ptr<wxMenu> m_popup;

        wxMenu *GetPopupMenu() override;
        std::unique_ptr<wxMenu> create_popup_menu();

        wxWindow& main_window() const;

        void on_exit(wxCommandEvent&);
        void on_open(wxCommandEvent&);
        void on_left_dclick(wxTaskBarIconEvent&);
};
