/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wusbip.h"
#include <wx/app.h>

namespace
{

class App : public wxApp
{
public:
        bool OnInit() override;
};


bool App::OnInit()
{
        if (!wxApp::OnInit()) {
                return false;
        }

        auto frame = new MainFrame;
        frame->Show(true);

        return true;
}

} // namespace


MainFrame::MainFrame() : Frame(nullptr)
{
}

wxIMPLEMENT_APP(App);
