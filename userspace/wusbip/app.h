/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wx/app.h>

class App : public wxApp
{
public:
        App();
        bool OnInit() override;

private:
        void set_names();
        void on_end_session(_In_ wxCloseEvent &event);
};

wxDECLARE_APP(App);
