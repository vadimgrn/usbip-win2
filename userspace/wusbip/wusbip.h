/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "frame.h"


#include <libusbip/vhci.h>
#include <thread>

class DeviceStateEvent;
wxDECLARE_EVENT(EVT_DEVICE_STATE, DeviceStateEvent); 

class MainFrame : public Frame
{
public:
	MainFrame(_In_ usbip::Handle read);
	~MainFrame();

	auto ok() const noexcept { return static_cast<bool>(m_iocp); }

private:
	enum { CompletionKey, CompletionKeyQuit, ConcurrentThreads = 1 };

	usbip::Handle m_read; // for ReadFile
	usbip::Handle m_iocp{ CreateIoCompletionPort(m_read.get(), nullptr, CompletionKey, ConcurrentThreads) };

	std::thread m_thread;

	void on_close(wxCloseEvent &event) override; 
	void on_exit(wxCommandEvent &event) override;
	void on_list(wxCommandEvent &event) override;
	void on_attach(wxCommandEvent &event) override;
	void on_detach(wxCommandEvent &event) override;
	void on_port(wxCommandEvent &event) override;
	void on_device_state(_In_ DeviceStateEvent &event);

	void log_last_error(_In_ const char *what, _In_ DWORD msg_id = GetLastError());
	
	void join();
	void read_loop();
	DWORD read(_In_ void *buf, _In_ DWORD len);
};
