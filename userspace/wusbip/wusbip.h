/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "frame.h"
#include "utils.h"

#include <thread>
#include <mutex>
#include <array>
#include <vector>
#include <set>

class LogWindow;
class wxDataViewColumn;

class DeviceStateEvent;
wxDECLARE_EVENT(EVT_DEVICE_STATE, DeviceStateEvent);

using device_columns = std::array<wxString, 9>; // indexed by visible columns only and DEV_COL_URL


class MainFrame : public Frame
{
public:
	MainFrame(_In_ usbip::Handle read);
	~MainFrame();

	static auto get_cmp_key(_In_ const device_columns &dc) noexcept;

private:
	enum { // hidden columns
		ID_COL_FIRST = ID_COL_BUSID, 
		ID_COL_LAST_VISIBLE = ID_COL_NOTES, 
		ID_COL_SAVED_STATE, 
		ID_COL_MAX };

	enum { // for device_columns only
		DEV_COL_URL = ID_COL_LAST_VISIBLE + 1, // use get_url()
		DEV_COL_CNT = DEV_COL_URL - int(ID_COL_FIRST) + 1
	};
	static_assert(std::tuple_size_v<device_columns> == DEV_COL_CNT);

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
	void set_log_level();

	void read_loop();
	void break_read_loop();

	wxTreeListItem find_server(_In_ const wxString &url, _In_ bool append);

	std::pair<wxTreeListItem, bool> find_or_add_device(_In_ const usbip::device_location &loc);
	std::pair<wxTreeListItem, bool> find_or_add_device(_In_ const wxString &url, _In_ const wxString &busid);
	std::pair<wxTreeListItem, bool> find_or_add_device(_In_ const device_columns &dc);

	void remove_device(_In_ wxTreeListItem dev);

	bool attach(_In_ const wxString &url, _In_ const wxString &busid);
	void post_refresh();

	bool is_persistent(_In_ wxTreeListItem device);
	void set_persistent(_In_ wxTreeListItem device, _In_ bool persistent);

	void update_device(_In_ wxTreeListItem device, _In_ const device_columns &dc, _In_ unsigned int flags);

	wxDataViewColumn& get_column(_In_ int col_id) const noexcept;
	int get_port(_In_ wxTreeListItem dev) const;

	wxTreeListItem get_edit_notes_device();
	
	static bool is_empty(_In_ const device_columns &dc) noexcept;
	static usbip::device_location make_device_location(_In_ const device_columns &dc);

	static std::pair<device_columns, unsigned int> make_device_columns(_In_ const usbip::device_state &st);
	static std::pair<device_columns, unsigned int> make_device_columns(_In_ const usbip::imported_device &dev);

	static auto& get_keys() noexcept;
	static std::vector<device_columns> get_saved();
	static unsigned int get_saved_flags() noexcept;

	static device_columns make_cmp_key(_In_ const usbip::device_location &loc);

	static unsigned int set_persistent_notes(_Inout_ device_columns &dc, _In_ unsigned int flags,
		_In_ const std::set<usbip::device_location> &persistent, _In_opt_ const std::set<device_columns> *saved = nullptr);

	static consteval auto mkflag(_In_ unsigned int col_id);
	static constexpr auto col(_In_ int col_id);

	template<auto col_id>
	static consteval auto col();

	template<typename Array>
	static auto& get_url(_In_ Array &v) noexcept;
};


inline constexpr auto MainFrame::col(_In_ int col_id)
{
	if (!std::is_constant_evaluated()) {
		wxASSERT(col_id >= static_cast<int>(ID_COL_FIRST));
		wxASSERT(col_id < static_cast<int>(ID_COL_MAX));
	}

	return static_cast<unsigned int>(col_id - ID_COL_FIRST);
}

template<auto col_id>
inline consteval auto MainFrame::col()
{
	static_assert(col_id >= static_cast<int>(ID_COL_FIRST));
	static_assert(col_id < static_cast<int>(ID_COL_MAX));

	return col(col_id);
}

inline consteval auto MainFrame::mkflag(_In_ unsigned int col_id) // ID_COL_XXX
{ 
	return 1U << col(col_id); 
}

template<typename Array>
inline auto& MainFrame::get_url(_In_ Array &v) noexcept 
{ 
	return v[col<DEV_COL_URL>()];
}

/*
 * @see make_cmp_key
 */
inline auto MainFrame::get_cmp_key(_In_ const device_columns &dc) noexcept
{
	return std::tie(get_url(dc), dc[col<ID_COL_BUSID>()]); // tuple of lvalue references
}

inline auto operator <=> (_In_ const device_columns &a, _In_ const device_columns &b)
{
	return MainFrame::get_cmp_key(a) <=> MainFrame::get_cmp_key(b);
}

inline auto operator == (_In_ const device_columns &a, _In_ const device_columns &b)
{
	return MainFrame::get_cmp_key(a) == MainFrame::get_cmp_key(b);
}
