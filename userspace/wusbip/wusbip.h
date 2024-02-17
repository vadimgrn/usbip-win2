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
	enum column_pos_t { // the order in the designer
		COL_BUSID,
		COL_PORT,
		COL_SPEED,
		COL_VENDOR,
		COL_PRODUCT,
		COL_STATE,
		COL_PERSISTENT,
		COL_NOTES,
		COL_LAST_VISIBLE = COL_NOTES,
		COL_SAVED_STATE, // hidden
	};

	enum { // for device_columns only
		DEV_COL_URL = COL_LAST_VISIBLE + 1, // use get_url()
		DEV_COL_CNT
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
	void restore_state();
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

	wxDataViewColumn* find_column(_In_ const wxString &title) const noexcept;
	wxDataViewColumn* find_column(_In_ int item_id) const noexcept;
	void set_menu_columns_labels();

	int get_port(_In_ wxTreeListItem dev) const;
	wxTreeListItem get_edit_notes_device();

	static bool is_empty(_In_ const device_columns &dc) noexcept;
	static usbip::device_location make_device_location(_In_ const device_columns &dc);

	static std::pair<device_columns, unsigned int> make_device_columns(_In_ const usbip::device_state &st);
	static std::pair<device_columns, unsigned int> make_device_columns(_In_ const usbip::imported_device &dev);

	static consteval auto get_saved_keys();
	static consteval auto get_saved_flags();
	static std::vector<device_columns> get_saved();

	static device_columns make_cmp_key(_In_ const usbip::device_location &loc);

	static unsigned int set_persistent_notes(_Inout_ device_columns &dc, _In_ unsigned int flags,
		_In_ const std::set<usbip::device_location> &persistent, _In_opt_ const std::set<device_columns> *saved = nullptr);

	static constexpr auto mkflag(_In_ column_pos_t col) { return 1U << col; }

	template<typename Array>
	static auto& get_url(_In_ Array &v) noexcept { return v[DEV_COL_URL]; }
};


inline consteval auto MainFrame::get_saved_keys()
{
	using key_val = std::pair<const wchar_t* const, unsigned int>;

	return std::to_array<key_val>({
		std::make_pair(L"busid", COL_BUSID),
		{ L"speed", COL_SPEED },
		{ L"vendor", COL_VENDOR },
		{ L"product", COL_PRODUCT },
		{ L"notes", COL_NOTES },
	});
}

inline consteval auto MainFrame::get_saved_flags()
{
	unsigned int flags{};

	for (auto [key, col]: get_saved_keys()) {
		flags |= 1U << col;
	}

	return flags;
}

/*
 * @see make_cmp_key
 */
inline auto MainFrame::get_cmp_key(_In_ const device_columns &dc) noexcept
{
	return std::tie(get_url(dc), dc[COL_BUSID]); // tuple of lvalue references
}

inline auto operator <=> (_In_ const device_columns &a, _In_ const device_columns &b)
{
	return MainFrame::get_cmp_key(a) <=> MainFrame::get_cmp_key(b);
}

inline auto operator == (_In_ const device_columns &a, _In_ const device_columns &b)
{
	return MainFrame::get_cmp_key(a) == MainFrame::get_cmp_key(b);
}
