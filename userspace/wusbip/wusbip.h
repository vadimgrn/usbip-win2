/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "frame.h"
#include <libusbip/vhci.h>

#include <thread>
#include <mutex>

class LogWindow;
class wxDataViewColumn;

class DeviceStateEvent;
wxDECLARE_EVENT(EVT_DEVICE_STATE, DeviceStateEvent);

class MainFrame : public Frame
{
public:
	MainFrame(_In_ usbip::Handle read);
	~MainFrame();

private:
	LogWindow *m_log{};

	usbip::Handle m_read;
	std::mutex m_read_close_mtx;

	std::thread m_read_thread{ &MainFrame::read_loop, this };

	void on_close(wxCloseEvent &event) override; 
	void on_exit(wxCommandEvent &event) override;
	void on_attach(wxCommandEvent &event) override;
	void on_detach(wxCommandEvent &event) override;
	void on_detach_all(wxCommandEvent &event) override;
	void on_refresh(wxCommandEvent &event) override;
	void on_log_level(wxCommandEvent &event) override;
	void on_help_about(wxCommandEvent &event) override;
	void add_exported_devices(wxCommandEvent &event) override;

	void on_log_show_update_ui(wxUpdateUIEvent &event) override;
	void on_log_show(wxCommandEvent &event) override;

	void on_view_column_update_ui(wxUpdateUIEvent &event) override;
	void on_view_column(wxCommandEvent &event) override;

	void on_device_state(_In_ DeviceStateEvent &event);

	void init();
	void set_log_level();

	void read_loop();
	void break_read_loop();

	wxTreeListItem find_server(_In_ const wxString &url, _In_ bool append);
	std::pair<wxTreeListItem, bool> find_device(_In_ const usbip::device_location &loc, _In_ bool append);
	void remove_device(_In_ wxTreeListItem dev);
	
	bool attach(_In_ const wxString &url, _In_ const wxString &busid);
	void post_refresh();

	bool is_persistent(_In_ wxTreeListItem device);

	void update_device(_In_ wxTreeListItem device, _In_ const usbip::device_state &state);
	void update_device(_In_ wxTreeListItem device, _In_ const usbip::imported_device &dev, _In_ usbip::state state);

	wxDataViewColumn& get_column(_In_ int col_id) const noexcept;

	static constexpr auto get_column_pos(_In_ int col_id)
	{
		if (!std::is_constant_evaluated()) {
			wxASSERT(col_id >= ID_COL_BUSID);
			wxASSERT(col_id <= ID_COL_COMMENTS);
		}

		return static_cast<unsigned int>(col_id - ID_COL_BUSID);
	}

	template<auto col_id>
	static consteval auto get_column_pos()
	{
		static_assert(col_id >= ID_COL_BUSID);
		static_assert(col_id <= ID_COL_COMMENTS);

		return get_column_pos(col_id);
	}
};
