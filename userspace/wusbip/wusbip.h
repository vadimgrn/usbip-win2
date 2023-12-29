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
	MainFrame(usbip::Handle read);

private:
	OVERLAPPED m_overlapped{ 0, 0, {0}, this }; // see ReadFileEx
	std::vector<char> m_read_buf = std::vector<char>( usbip::vhci::get_device_state_size() );
	usbip::Handle m_read; // for ReadFileEx

	void on_exit(wxCommandEvent &event) override;
	void on_list(wxCommandEvent &event) override;
	void on_attach(wxCommandEvent &event) override;
	void on_detach(wxCommandEvent &event) override;
	void on_port(wxCommandEvent &event) override;

	void on_idle(wxIdleEvent &event) override;

	void log_last_error(const char *what, DWORD msg_id = GetLastError());
	
	bool async_read();
	void on_read(DWORD errcode);
	
	static void on_read(DWORD errcode, DWORD /*NumberOfBytesTransfered*/, OVERLAPPED *overlapped)
	{
		static_cast<MainFrame*>(overlapped->hEvent)->on_read(errcode);
	}

	void state_changed(const usbip::device_state &st);
};
