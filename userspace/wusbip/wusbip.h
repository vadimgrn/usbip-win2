﻿/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "frame.h"
#include "device_columns.h"

#include <libusbip/win_handle.h>

#include <thread>
#include <mutex>

#include <wx/log.h>

class wxDataViewColumn;

class DeviceStateEvent;
wxDECLARE_EVENT(EVT_DEVICE_STATE, DeviceStateEvent);


/*
 * Do not show dialog box for wxLOG_Info aka Verbose.
 */
class LogWindow : public wxLogWindow
{
public:
	LogWindow(_In_ wxWindow *parent, _In_ const wxMenuItem *log_toogle);

private:
	void DoLogRecord(_In_ wxLogLevel level, _In_ const wxString &msg, _In_ const wxLogRecordInfo &info) override;
};


class MainFrame : public Frame
{
public:
	MainFrame(_In_ usbip::Handle read);
	~MainFrame();

private:
	friend class wxPersistentMainFrame;
	LogWindow *m_log{};

	usbip::Handle m_read;
	std::mutex m_read_close_mtx;

	std::thread m_read_thread{ &MainFrame::read_loop, this };

	void on_close(wxCloseEvent &event) override; 
	void on_exit(wxCommandEvent &event) override;

	void on_attach(wxCommandEvent &event) override;
	void on_detach(wxCommandEvent &event) override;
	void on_detach_all(wxCommandEvent &event) override;
	void on_reload(wxCommandEvent &event) override;
	void on_log_level(wxCommandEvent &event) override;
	void on_help_about(wxCommandEvent &event) override;
	void add_exported_devices(wxCommandEvent &event) override;
	void on_select_all(wxCommandEvent &event) override;
	void on_has_items_update_ui(wxUpdateUIEvent &event) override;
	void on_has_selections_update_ui(wxUpdateUIEvent &event) override;
	void on_toogle_persistent(wxCommandEvent &event) override;

	void on_save(wxCommandEvent &event) override;
	void on_load(wxCommandEvent &event) override;

	void on_log_show_update_ui(wxUpdateUIEvent &event) override;
	void on_log_show(wxCommandEvent &event) override;

	void on_view_column_update_ui(wxUpdateUIEvent &event) override;
	void on_view_column(wxCommandEvent &event) override;

	void on_device_state(_In_ DeviceStateEvent &event);
	void on_item_activated(wxTreeListEvent &event) override;

	void on_view_labels(wxCommandEvent &event) override;
	void on_view_labels_update_ui(wxUpdateUIEvent &event) override;

	void on_edit_notes(wxCommandEvent &event) override;
	void on_edit_notes_update_ui(wxUpdateUIEvent &event) override;

	void init();
	void restore_state();
	void post_restore_state();
	void adjust_log_level();

	void read_loop();
	void break_read_loop();

	wxTreeListItem find_or_add_server(_In_ const wxString &url);

	std::pair<wxTreeListItem, bool> find_or_add_device(_In_ const wxString &url, _In_ const wxString &busid);
	std::pair<wxTreeListItem, bool> find_or_add_device(_In_ const usbip::device_location &loc);
	std::pair<wxTreeListItem, bool> find_or_add_device(_In_ const usbip::device_columns &dc);

	void remove_device(_In_ wxTreeListItem dev);

	bool attach(_In_ const wxString &url, _In_ const wxString &busid);
	void post_refresh();

	bool is_persistent(_In_ wxTreeListItem device);
	void set_persistent(_In_ wxTreeListItem device, _In_ bool persistent);

	void update_device(_In_ wxTreeListItem device, _In_ const usbip::device_columns &dc, _In_ unsigned int flags);

	wxDataViewColumn* find_column(_In_ const wxString &title) const noexcept;
	wxDataViewColumn* find_column(_In_ int item_id) const noexcept;
	void set_menu_columns_labels();

	int get_port(_In_ wxTreeListItem dev) const;
	wxTreeListItem get_edit_notes_device();
};
