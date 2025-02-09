/*
 * Copyright (C) 2023 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "frame.h"
#include "device_columns.h"
#include "tree_comparator.h"

#include <libusbip/win_handle.h>

#include <thread>
#include <mutex>

class wxLogWindow;
class TaskBarIcon;
class wxDataViewColumn;

class DeviceStateEvent;
wxDECLARE_EVENT(EVT_DEVICE_STATE, DeviceStateEvent);


class MainFrame : public Frame
{
public:
	MainFrame(_In_ usbip::Handle read);
	~MainFrame();

	auto start_in_tray() const noexcept { return m_start_in_tray; }
	void iconize_to_tray();

	void on_exit(wxCommandEvent &event) override;
	void on_detach_all(wxCommandEvent &event) override;

	auto& get_menu_devices() const noexcept { return *m_menu_devices; }
	auto& get_menu_file() const noexcept { return *m_menu_file; }

private:
	friend class wxPersistentMainFrame;
	enum { IMG_SERVER, IMG_DEVICE, IMG_CNT };

	bool m_start_in_tray{};
	bool m_close_to_tray{};

	wxLogWindow *m_log{};
	TreeListItemComparator m_tree_cmp;
	std::unique_ptr<TaskBarIcon> m_taskbar_icon;
	std::unique_ptr<wxMenu> m_tree_popup_menu;

	usbip::Handle m_read;
	std::mutex m_read_close_mtx;

	std::thread m_read_thread{ &MainFrame::read_loop, this };

	static wxWithImages::Images get_tree_images();

	void on_close(wxCloseEvent &event) override; 
	void on_attach(wxCommandEvent &event) override;
	void on_detach(wxCommandEvent &event) override;
	void on_reload(wxCommandEvent &event) override;
	void on_help_about(wxCommandEvent &event) override;
	void add_exported_devices(wxCommandEvent &event) override;
	void on_select_all(wxCommandEvent &event) override;
	void on_has_devices_update_ui(wxUpdateUIEvent &event) override;
	void on_has_selected_devices_update_ui(wxUpdateUIEvent &event) override;
	void on_toogle_auto(wxCommandEvent &event) override;
	void on_item_context_menu(wxTreeListEvent &event) override;
	void on_view_reset(wxCommandEvent &event) override;
	void on_help_about_lib(wxCommandEvent&) override;
	void on_copy_rows(wxCommandEvent &event) override;

	void on_save(wxCommandEvent &event) override;
	void on_save_selected(wxCommandEvent &event) override;
	void on_load(wxCommandEvent &event) override;

	void on_view_zebra_update_ui(wxUpdateUIEvent &event) override;
	void on_view_zebra(wxCommandEvent &event) override;

	void on_log_show_update_ui(wxUpdateUIEvent &event) override;
	void on_log_show(wxCommandEvent &event) override;

	void on_log_library_update_ui(wxUpdateUIEvent &event) override;
	void on_log_library(wxCommandEvent &event) override;

	void on_log_verbose_update_ui(wxUpdateUIEvent &event) override;
	void on_log_verbose(wxCommandEvent &event) override;

	void on_view_column_update_ui(wxUpdateUIEvent &event) override;
	void on_view_column(wxCommandEvent &event) override;

	void on_start_in_tray_update_ui(wxUpdateUIEvent &event) override;
	void on_start_in_tray(wxCommandEvent &event) override;
	
	void on_close_to_tray_update_ui(wxUpdateUIEvent &event) override;
	void on_close_to_tray(wxCommandEvent &event) override;

	void on_device_state(_In_ DeviceStateEvent &event);
	void on_item_activated(wxTreeListEvent &event) override;

	void on_view_labels(wxCommandEvent &event) override;
	void on_view_labels_update_ui(wxUpdateUIEvent &event) override;

	void on_edit_notes(wxCommandEvent &event) override;
	void on_edit_notes_update_ui(wxUpdateUIEvent &event) override;

	void on_frame_mouse_wheel(wxMouseEvent &event) override;

	void on_view_font_increase(wxCommandEvent & event) override;
	void on_view_font_decrease(wxCommandEvent & event) override;
	void on_view_font_default(wxCommandEvent &event) override;

	void init();
	void init_tree_list();
	void restore_state();

	void read_loop();
	void break_read_loop();

	wxTreeListItem find_or_add_server(_In_ const wxString &url);

	std::pair<wxTreeListItem, bool> find_or_add_device(_In_ const wxString &url, _In_ const wxString &busid);
	std::pair<wxTreeListItem, bool> find_or_add_device(_In_ const usbip::device_location &loc);
	std::pair<wxTreeListItem, bool> find_or_add_device(_In_ const usbip::device_columns &dc);

	void remove_device(_In_ wxTreeListItem dev);
	DWORD attach(_In_ const wxString &url, _In_ const wxString &busid);
	
	void post_refresh();
	void post_exit();

	bool is_persistent(_In_ wxTreeListItem device);
	void set_persistent(_In_ wxTreeListItem device, _In_ bool persistent);

	void update_device(_In_ wxTreeListItem device, _In_ const usbip::device_columns &dc, _In_ unsigned int flags);
	
	auto connect(_In_ const wxString &hostname, _In_ const wxString &service, 
		              _In_ const std::string &hostname_u8, _In_ const std::string &service_u8);

	wxDataViewColumn* find_column(_In_ const wxString &title) const noexcept;
	wxDataViewColumn* find_column(_In_ int item_id) const noexcept;
	void set_menu_columns_labels();

	int get_port(_In_ wxTreeListItem dev) const;
	wxTreeListItem get_edit_notes_device();

	void save(_In_ const wxTreeListItems &devices);
	void on_tree_mouse_wheel(_In_ wxMouseEvent &event);

	using menu_item_descr = std::tuple<int, wxMenu*, decltype(&MainFrame::on_attach)>;
	std::unique_ptr<wxMenu> create_menu(_In_ const menu_item_descr *items, _In_ int cnt);

	std::unique_ptr<wxMenu> create_tree_popup_menu();
};
