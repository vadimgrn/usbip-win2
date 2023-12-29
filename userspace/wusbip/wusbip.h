/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "frame.h"
#include <libusbip/vhci.h>

#include <vector>

class MainFrame : public Frame
{
public:
	MainFrame() : Frame(nullptr) {}
private:
	OVERLAPPED m_overlapped{ 0, 0, {0}, this }; // see ReadFileEx
	std::vector<char> m_read_buf = std::vector<char>( usbip::vhci::get_device_state_size() );

	void on_exit(wxCommandEvent &event) override;
	void on_list(wxCommandEvent &event) override;
	void on_attach(wxCommandEvent &event) override;
	void on_detach(wxCommandEvent &event) override;
	void on_port(wxCommandEvent &event) override;

	bool async_read();
	static void on_read(DWORD errcode, DWORD NumberOfBytesTransfered, OVERLAPPED *overlapped);

	void state_changed(const usbip::device_state &st);
};
