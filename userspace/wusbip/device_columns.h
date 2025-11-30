/*
 * Copyright (c) 2024-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wx/string.h>
#include <array>

namespace usbip
{

struct device_location;
struct imported_device;
struct device_state;

enum column_pos_t { // columns order in the tree
	COL_BUSID,
	COL_PORT,
	COL_SPEED,
	COL_VENDOR,
	COL_PRODUCT,
	COL_STATE,
	COL_PERSISTENT,
	COL_NOTES,
	COL_DEVID,
	COL_LAST_VISIBLE = COL_DEVID,
	COL_SAVED_STATE, // hidden

	UPD_COL_FIRST = COL_PORT,
	UPD_COL_LAST  = COL_LAST_VISIBLE,
};

enum { // for device_columns only
	DEV_COL_URL = COL_LAST_VISIBLE + 1, // use get_url()
	DEV_COL_CNT
};

using device_columns = std::array<wxString, DEV_COL_CNT>; // indexed by visible columns only and DEV_COL_URL

template<typename Array>
constexpr auto& get_url(_In_ Array &v) { return v[DEV_COL_URL]; }

constexpr auto mkflag(_In_ column_pos_t col) { return 1U << col; }

constexpr auto mkflags(_In_ std::initializer_list<column_pos_t> columns) noexcept
{
	auto flags = 0U;
	for (auto col: columns) {
		flags |= mkflag(col);
	}
	return flags;
}
static_assert(mkflags({COL_PORT, COL_SPEED, COL_VENDOR}) == 0b1110);

device_location make_device_location(_In_ const device_columns &dc);
device_location make_device_location(_In_ const wxString &url, _In_ const wxString &busid);

std::pair<device_columns, unsigned int> make_device_columns(_In_ const device_state &st);
std::pair<device_columns, unsigned int> make_device_columns(_In_ const imported_device &dev);

bool is_empty(_In_ const imported_device &d) noexcept;

/*
 * @see make_cmp_key
 */
constexpr auto get_cmp_key(_In_ const device_columns &dc)
{
	return std::tie(get_url(dc), dc[COL_BUSID]); // tuple of lvalue references
}

constexpr auto operator == (_In_ const device_columns &a, _In_ const device_columns &b)
{
	return get_cmp_key(a) == get_cmp_key(b);
}

constexpr auto operator <=> (_In_ const device_columns &a, _In_ const device_columns &b)
{
	return get_cmp_key(a) <=> get_cmp_key(b);
}

} // namespace usbip


namespace std
{
	using usbip::operator ==; // ADL does not help because std::tuple is defined in namespace std
	using usbip::operator <=>;
}
