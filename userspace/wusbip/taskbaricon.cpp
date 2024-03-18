/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "taskbaricon.h"
#include "app.h"

#include <wx/menu.h>
#include <wx/window.h>

TaskBarIcon::TaskBarIcon()
{
        Bind(wxEVT_TASKBAR_LEFT_DCLICK, wxTaskBarIconEventHandler(TaskBarIcon::on_left_dclick), this);
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

        if (auto item = m->Append(wxID_ANY, _("Open window"), _("Open the main window"))) {
                Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TaskBarIcon::on_open), this, item->GetId());
        }

        m->AppendSeparator();

        if (auto item = m->Append(wxID_ANY, _("E&xit"), _("Exit the app"))) {
                Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TaskBarIcon::on_exit), this, item->GetId());
        }

        return m;
}

wxWindow& TaskBarIcon::window() const
{
        return *wxGetApp().GetMainTopWindow();
}

void TaskBarIcon::on_exit(wxCommandEvent&)
{ 
        window().Close(true);
}

void TaskBarIcon::on_open(wxCommandEvent&)
{
        wxASSERT(!window().IsShown());
        window().Show();

        wxASSERT(IsIconInstalled());

        [[maybe_unused]] auto ok = RemoveIcon();
        wxASSERT(ok);
}

void TaskBarIcon::on_left_dclick(wxTaskBarIconEvent&)
{
        wxCommandEvent evt;
        on_open(evt);
}
