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
        void write_appearance(_In_ int val);

private:
        static inline auto &m_appearance = L"Appearance";

        void set_names();
        Appearance set_appearance();
        void on_end_session(_In_ wxCloseEvent &event);
};

wxDECLARE_APP(App);
