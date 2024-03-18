/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "app.h"
#include "utils.h"
#include "wusbip.h"

#include <libusbip/src/file_ver.h>

using namespace usbip;

bool App::OnInit()
{
        if (wxApp::OnInit()) {
                set_names();
        } else {
                return false;
        }

        wxString err;

        if (auto read = init(err) ? vhci::open() : Handle()) {
                auto frame = new MainFrame(std::move(read));
                frame->Show(true);
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

wxIMPLEMENT_APP(App);
