/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "taskbaricon.h"
#include "wusbip.h"
#include "app.h"

TaskBarIcon::TaskBarIcon()
{
        Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TaskBarIcon::on_show), this, wxID_EXECUTE);
        Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TaskBarIcon::on_exit), this, wxID_EXIT);
        Bind(wxEVT_TASKBAR_LEFT_DCLICK, wxTaskBarIconEventHandler(TaskBarIcon::on_left_dclick), this);
}

wxWindow& TaskBarIcon::get_main_window() const 
{ 
        return *wxGetApp().GetMainTopWindow(); 
}

void TaskBarIcon::on_exit(wxCommandEvent&) 
{ 
        get_main_window().Close(true); 
}

wxMenu* TaskBarIcon::GetPopupMenu()
{
        if (!m_popup) {
                m_popup = create_popup_menu();
        }
        return m_popup.get();
}

std::unique_ptr<wxMenu> TaskBarIcon::create_popup_menu()
{
        auto m = std::make_unique<wxMenu>();

        m->AppendCheckItem(wxID_EXECUTE, _("Show"), _("Show window"));
        Connect(wxID_EXECUTE, wxEVT_UPDATE_UI, wxUpdateUIEventHandler(TaskBarIcon::on_show_update_ui));

        m->Append(wxID_EXIT, _("Exit"), nullptr, _("Close app"));

        return m;
}

void TaskBarIcon::on_show_update_ui(wxUpdateUIEvent &event)
{
        auto shown = get_main_window().IsShown();
        event.Check(shown);
}

void TaskBarIcon::on_left_dclick(wxTaskBarIconEvent&)
{
        wxCommandEvent evt;
        evt.SetInt(1);

        on_show(evt);
}

void TaskBarIcon::on_show(wxCommandEvent &event)
{
        bool checked = event.GetInt();
        get_main_window().Show(checked);
        
        if (IsIconInstalled()) {
                RemoveIcon();
        }
}
