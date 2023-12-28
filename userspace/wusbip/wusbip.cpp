/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wusbip.h"
#include "utils.h"

#include <wx/app.h>
#include <wx/msgdlg.h>

#include <libusbip/vhci.h>
#include <libusbip/remote.h>

namespace
{

using namespace usbip;

class App : public wxApp
{
public:
        bool OnInit() override;
};

bool App::OnInit()
{
        if (!get_vhci()) {
                auto s = GetLastErrorMsg();
                wxMessageBox(s, _("Critical error"), wxICON_ERROR);
                return false;
        }

        if (!wxApp::OnInit()) {
                return false;
        }

        auto frame = new MainFrame;
        frame->Show(true);

        return true;
}

} // namespace


wxIMPLEMENT_APP(App);

void MainFrame::on_exit(wxCommandEvent&)
{
        Close(true);
}

void MainFrame::on_list(wxCommandEvent&)
{
        wxMessageBox(__func__);
}

void MainFrame::on_attach(wxCommandEvent&)
{
        wxMessageBox(__func__);
}

void MainFrame::on_detach(wxCommandEvent&)
{
        wxMessageBox(__func__);
}

void MainFrame::on_port(wxCommandEvent&)
{
        wxMessageBox(__func__);
}
