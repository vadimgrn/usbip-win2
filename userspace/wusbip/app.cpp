/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "app.h"
#include "utils.h"
#include "wusbip.h"

#include <libusbip/src/file_ver.h>
#include <wx/config.h>

namespace
{

using namespace usbip;
        
auto read_appearance()
{
        enum { System = static_cast<long>(wxApp::Appearance::System) };

        auto &cfg = *wxConfig::Get();
        auto val = cfg.ReadLong(App::KeyAppearance, System);

        switch (val) {
        case System:
        case static_cast<long>(wxApp::Appearance::Light):
        case static_cast<long>(wxApp::Appearance::Dark):
                break;
        default:
                wxLogError(_("Unexpected value %s=%d"), App::KeyAppearance, val);
                val = System;
        }

        return static_cast<wxApp::Appearance>(val);
}

auto init_mainframe(_In_ wxApp::Appearance appearance)
{
        wxString err;

        if (auto read = usbip::init(err) ? vhci::open() : Handle()) {
                if (auto &frame = *new MainFrame(std::move(read), appearance); frame.start_in_tray()) {
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

} // namespace


App::App()
{
        Bind(wxEVT_END_SESSION, &App::on_end_session, this);
}

bool App::OnInit()
{
        if (!wxApp::OnInit()) {
                return false;
        }

        set_names();

        auto appearance = restore_appearance();
        return init_mainframe(appearance);
}

void App::set_names()
{
        auto &v = win::get_file_version();

        SetAppName(wx_string(v.GetProductName()));
        SetVendorName(wx_string(v.GetCompanyName()));
}

auto App::restore_appearance() -> Appearance
{
        auto appearance = read_appearance();

        if (auto res = SetAppearance(appearance); res != AppearanceResult::Ok) {
                wxLogError(_("SetAppearance(%d) error %d"), appearance, res);
        }

        return appearance;
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
