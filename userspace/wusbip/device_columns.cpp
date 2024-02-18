/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device_columns.h"
#include "utils.h"

#include <libusbip/vhci.h>
#include <libusbip/src/usb_ids.h>

#include <wx/translation.h>

/*
 * @see is_empty(const imported_device&)
 */
bool usbip::is_empty(_In_ const device_columns &dc) noexcept
{
	for (auto col: {COL_VENDOR, COL_PRODUCT}) {
		if (dc[col].empty()) {
			return true;
		}
	}

	return false;
}

auto usbip::make_device_location(_In_ const wxString &url, _In_ const wxString &busid) -> device_location
{
        wxString hostname;
        wxString service;
        split_server_url(url, hostname, service);

        return device_location {
                .hostname = hostname.ToStdString(wxConvUTF8), 
                .service = service.ToStdString(wxConvUTF8), 
                .busid = busid.ToStdString(wxConvUTF8), 
        };
}

auto usbip::make_device_location(_In_ const device_columns &dc) -> device_location
{
	auto &url = get_url(dc);
	return make_device_location(url, dc[COL_BUSID]);
}

/*
 * @see get_cmp_key 
 */
auto usbip::make_cmp_key(_In_ const device_location &loc) -> device_columns
{
        device_columns dc;

        get_url(dc) = make_server_url(loc);
        dc[COL_BUSID] = wxString::FromUTF8(loc.busid);

        return dc;
}

auto usbip::make_device_columns(_In_ const imported_device &dev) -> std::pair<device_columns, unsigned int> 
{
        auto dc = make_cmp_key(dev.location);

        if (auto &port = dc[COL_PORT]; dev.port) {
                port = wxString::Format(L"%02d", dev.port); // XX for proper sorting
        } else {
                port.clear();
        }

        dc[COL_SPEED] = get_speed_str(dev.speed);

        auto str = [] (auto id, auto sv)
        {
                return sv.empty() ? wxString::Format(L"%04x", id) : wxString::FromAscii(sv.data(), sv.size());
        };

        auto [vendor, product] = get_ids().find_product(dev.vendor, dev.product);

        dc[COL_VENDOR] = str(dev.vendor, vendor);
        dc[COL_PRODUCT] = str(dev.product, product);

        return { std::move(dc), mkflags({COL_PORT, COL_SPEED, COL_VENDOR, COL_PRODUCT}) };
}

auto usbip::make_device_columns(_In_ const device_state &st) -> std::pair<device_columns, unsigned int> 
{
        auto ret = make_device_columns(st.device);
        auto &[dc, flags] = ret;

        dc[COL_STATE] = _(vhci::get_state_str(st.state));
        flags |= mkflag(COL_STATE);

        return ret;
}
