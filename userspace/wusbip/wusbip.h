/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "frame.h"
#include <libusbip/vhci.h>

#include <thread>
#include <mutex>

class LogWindow;

class DeviceStateEvent;
wxDECLARE_EVENT(EVT_DEVICE_STATE, DeviceStateEvent);

class MainFrame : public Frame
{
public:
	MainFrame(_In_ usbip::Handle read);
	~MainFrame();

private:
	enum { COL_BUSID, COL_PORT, COL_SPEED, COL_VID, COL_PID, COL_STATE }; // m_treeListCtrl
	LogWindow *m_log{};

	usbip::Handle m_read;
	std::mutex m_read_close_mtx;

	std::thread m_read_thread{ &MainFrame::read_loop, this };

	void on_close(wxCloseEvent &event) override; 
	void on_exit(wxCommandEvent &event) override;
	void on_list(wxCommandEvent &event) override;
	void on_attach(wxCommandEvent &event) override;
	void on_detach(wxCommandEvent &event) override;
	void on_refresh(wxCommandEvent &event) override;
	void on_log_level(wxCommandEvent &event) override;
	void on_help_about(wxCommandEvent &event) override;

	void on_log_show_update_ui(wxUpdateUIEvent &event) override;
	void on_log_show(wxCommandEvent &event) override;
	
	void on_device_state(_In_ DeviceStateEvent &event);
	void set_log_level();

	void read_loop();
	void break_read_loop();

	wxTreeListItem find_server(_In_ const usbip::device_location &loc, _In_ bool append);
	std::pair<wxTreeListItem, bool> find_device(_In_ const usbip::device_location &loc, _In_ bool append);
	void remove_device(_In_ wxTreeListItem dev);

	void update_device(_In_ wxTreeListItem dev, _In_ const usbip::device_state &state);
	void update_device(_In_ wxTreeListItem dev, _In_ const usbip::imported_device &d);
};
