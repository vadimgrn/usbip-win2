/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "app.h"
#include "utils.h"
#include "wusbip.h"

#include <libusbip/src/file_ver.h>

using namespace usbip;

App::App()
{
        Bind(wxEVT_END_SESSION, &App::on_end_session, this);
}

bool App::OnInit()
{
        if (wxApp::OnInit()) {
                set_names();
        } else {
                return false;
        }

        wxString err;

        if (auto read = init(err) ? vhci::open() : Handle()) {
                if (auto &frame = *new MainFrame(std::move(read)); frame.start_in_tray()) {
                        frame.iconize_to_tray();
                } else {
                        frame.Show();
                }
                return true;
        }

        if (err.empty()) {
                err = GetLastErrorMsg();
        }

        wxSafeShowMessage(_("Fatal error"), err);
        return false;
}

void App::set_names()
{
        auto &v = win::get_file_version();

        SetAppName(wx_string(v.GetProductName()));
        SetVendorName(wx_string(v.GetCompanyName()));
}

/*
 * wxEVT_CLOSE_WINDOW will not be sent to MainFrame, but m_read_thread must be joined.
 * @see MainFrame::on_close
 */
void App::on_end_session(_In_ wxCloseEvent&)
{
        if (auto wnd = GetMainTopWindow()) {
                wnd->Close(true);
        }
}

wxIMPLEMENT_APP(App);
