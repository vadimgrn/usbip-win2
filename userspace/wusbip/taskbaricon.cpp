/*
 * Copyright (c) 2024-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
        Bind(wxEVT_TASKBAR_BALLOON_TIMEOUT, wxTaskBarIconEventHandler(TaskBarIcon::on_balloon_timeout), this);
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

        struct {
                int id;
                wxEventFunction f;
                wxMenu *menu;
        } const cmds[] {
                {},
                { MainFrame::ID_ATTACH_STOP_ALL, wxCommandEventHandler(MainFrame::on_attach_stop_all), fr.m_menu_devices },
                { wxID_CLOSE_ALL, wxCommandEventHandler(MainFrame::on_detach_all), fr.m_menu_devices },
                {},
                { wxID_EXIT, wxCommandEventHandler(MainFrame::on_exit), fr.m_menu_file },
        };

        for (auto [id, handler, src]: cmds) {
                if (!id) {
                        menu->AppendSeparator();
                } else if (clone_menu_item(*menu, id, *src)) {
                        Bind(wxEVT_COMMAND_MENU_SELECTED, handler, &fr, id);
                }
        }

        return menu;
}

MainFrame& TaskBarIcon::frame() const
{
        return *wxStaticCast(wxGetApp().GetMainTopWindow(), MainFrame);
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

void TaskBarIcon::show_balloon(_In_ const wxString &text, _In_ int flags)
{
        wxLogVerbose(_("Balloon '%s', cancel %d"), text, m_cancel);

        if (m_cancel) {
                auto ok = ShowBalloon(wxEmptyString, wxEmptyString, 0, 0); 
                wxASSERT(ok);
        }

        m_cancel = ShowBalloon(wxEmptyString, text, 0, flags);
        wxASSERT(m_cancel);
}
