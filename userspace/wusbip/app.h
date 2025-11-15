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

        static inline auto &KeyAppearance = L"Appearance";

private:
        void set_names();
        Appearance restore_appearance();
        void on_end_session(_In_ wxCloseEvent &event);
};

wxDECLARE_APP(App);
