/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "frame.h"

class MainFrame : public Frame
{
public:
	MainFrame() : Frame(nullptr) {}
private:
	void on_exit(wxCommandEvent &event) override;
	void on_list(wxCommandEvent &event) override;
	void on_attach(wxCommandEvent &event) override;
	void on_detach(wxCommandEvent &event) override;
	void on_port(wxCommandEvent &event) override;
};
