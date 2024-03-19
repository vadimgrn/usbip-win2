/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "taskbaricon.h"
#include "wusbip.h"
#include "app.h"
#include "wxutils.h"

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
        auto &fr = frame();
        auto menu = std::make_unique<wxMenu>();

        if (auto item = menu->Append(wxID_ANY, _("Open window"), _("Open the main window"))) {
                Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(TaskBarIcon::on_open), this, item->GetId());
                item->SetBitmaps(wxArtProvider::GetBitmap(wxASCII_STR(wxART_FULL_SCREEN), wxASCII_STR(wxART_MENU)));
        }

        menu->AppendSeparator();

        if (auto id = wxID_CLOSE_ALL; clone_menu_item(*menu, id, fr.get_menu_devices())) {
                Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(MainFrame::on_detach_all), &fr, id);
        }

        if (auto id = wxID_EXIT; clone_menu_item(*menu, id, fr.get_menu_file())) {
                Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(MainFrame::on_exit), &fr, id);
        }

        return menu;
}

MainFrame& TaskBarIcon::frame() const
{
        return *static_cast<MainFrame*>(wxGetApp().GetMainTopWindow());
}

void TaskBarIcon::on_open(wxCommandEvent&)
{
        wxASSERT(!frame().IsShown());
        frame().Show();

        wxASSERT(IsIconInstalled());

        [[maybe_unused]] auto ok = RemoveIcon();
        wxASSERT(ok);
}

void TaskBarIcon::on_left_dclick(wxTaskBarIconEvent&)
{
        wxCommandEvent evt;
        on_open(evt);
}
