/*
 * Copyright (c) 2024-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device_columns.h"
#include "utils.h"

#include <libusbip/vhci.h>
#include <libusbip/src/usb_ids.h>

#include <wx/translation.h>

namespace
{

using namespace usbip;


/*
 * @see get_cmp_key
 */
auto make_cmp_key(_In_ const device_location &loc)
{
        device_columns dc;

        get_url(dc) = make_server_url(loc);
        dc[COL_BUSID] = wxString::FromUTF8(loc.busid);

        return dc;
}

/*
 * @param vendor_id  can be zero
 * @param product_id can be zero
 */
void set_vendor_product(_Inout_ device_columns &dc, _In_ UINT16 vendor_id, _In_ UINT16 product_id) 
{
        auto [vendor, product] = get_ids().find_product(vendor_id, product_id);

        for (auto [col, id, str]: { std::make_tuple(COL_VENDOR, vendor_id, vendor),
                                    std::make_tuple(COL_PRODUCT, product_id, product) }) {

                if (id) {
                        dc[col] = str.empty() ? wxString::Format(L"%08x", id) : wxString::FromAscii(str.data(), str.size());
                }
        }
}

} // namespace


/*
 * port, vendor, product can be zero, speed can be UsbLowSpeed aka zero.
 */
bool usbip::is_empty(_In_ const imported_device &d) noexcept
{
        return !d.devid;
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

auto usbip::make_device_columns(_In_ const imported_device &dev) ->
        std::pair<device_columns, unsigned int> 
{
        auto res = std::make_pair(make_cmp_key(dev.location), 0U);
        auto& [dc, flags] = res;

        if (is_empty(dev)) {
                wxASSERT(!dev.speed);
                wxASSERT(!dev.vendor);
                wxASSERT(!dev.product);
                wxASSERT(!dev.port);
                return res;
        }
        
        dc[COL_DEVID] = wxString::Format(L"%08x", dev.devid);
        dc[COL_SPEED] = get_speed_str(dev.speed); // do not use _(), get_speed_val must return original value

        set_vendor_product(dc, dev.vendor, dev.product);
        flags |= mkflags({COL_DEVID, COL_SPEED, COL_VENDOR, COL_PRODUCT});

        if (dev.port) {
                dc[COL_PORT] = wxString::Format(L"%03d", dev.port); // XXX for proper sorting
                flags |= mkflag(COL_PORT);
        }

        return res;
}

auto usbip::make_device_columns(_In_ const device_state &st) ->
        std::pair<device_columns, unsigned int> 
{
        auto ret = make_device_columns(st.device);
        auto &[dc, flags] = ret;

        dc[COL_STATE] = to_string(st.state);
        flags |= mkflag(COL_STATE);

        return ret;
}
