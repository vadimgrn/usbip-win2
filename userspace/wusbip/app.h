/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wx/app.h>

class App : public wxApp
{
public:
        bool OnInit() override;

private:
        void set_names();
};

wxDECLARE_APP(App);
