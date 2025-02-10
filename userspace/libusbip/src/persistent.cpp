/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "..\persistent.h"
#include "..\vhci.h"
#include "output.h"

#include <usbip\vhci.h>
#include <ranges>

namespace
{

using namespace usbip;

auto is_malformed(_In_ const device_location &d)
{
        return d.hostname.empty() || d.service.empty() || d.busid.empty();
}

auto make_multi_sz(_Inout_ std::wstring &result, _In_ const std::vector<device_location> &v)
{
        assert(result.empty());

        for (auto &i: v) {
                if (is_malformed(i)) {
                        libusbip::output("malformed device_location{ hostname='{}', service='{}', busid='{}' }", 
                                          i.hostname, i.service, i.busid);

                        return ERROR_INVALID_PARAMETER;
                }

                auto s = i.hostname + ',' + i.service + ',' + i.busid + '\0';
                result += utf8_to_wchar(s);
        }

        result += L'\0';
        return ERROR_SUCCESS;
}

auto parse_device_location(_In_ const std::string &str)
{
        device_location dl;

        auto f = [] (auto r) { return std::string_view(r.begin(), r.end()); };
        auto v = str | std::views::split(',') | std::views::transform(std::move(f));

        if (auto i = v.begin(), end = v.end(); i != end) {
                dl.hostname = *i;
                if (++i != end) {
                        dl.service = *i;
                        if (++i != end) {
                                dl.busid.assign((*i).data(), &str.back() + 1); // tail
                        }
                }
        }

        return dl;
}

auto get_persistent_devices(_In_ HANDLE dev, _Out_ bool &success)
{
        std::wstring val(512, L'\0');

        for (DWORD BytesReturned{}; ; ) { // must be set if the last arg is NULL

                success = DeviceIoControl(dev, vhci::ioctl::GET_PERSISTENT, nullptr, 0, 
                                          val.data(), DWORD(size_bytes(val)), &BytesReturned, nullptr);

                if (success || GetLastError() == ERROR_MORE_DATA) { // WdfRegistryQueryValue -> STATUS_BUFFER_OVERFLOW
                        val.resize(BytesReturned/sizeof(val[0]));
                        if (success) {
                                break;
                        }
                } else {
                        val.clear();
                        break;
                }
        }

        return val;
}

} // namespace


bool usbip::vhci::set_persistent(_In_ HANDLE dev, _In_ const std::vector<device_location> &devices)
{
        std::wstring val;
        if (auto err = ::make_multi_sz(val, devices)) {
                SetLastError(err);
                return false;
        }

        DWORD BytesReturned{}; // must be set if the last arg is NULL
        auto ok = DeviceIoControl(dev, ioctl::SET_PERSISTENT, val.data(), DWORD(size_bytes(val)),
                                  nullptr, 0, &BytesReturned, nullptr);

        assert(!BytesReturned);
        return ok;
}

auto usbip::vhci::get_persistent(_In_ HANDLE dev, _Out_ bool &success) -> std::vector<device_location>
{
        std::vector<device_location> devs;

        auto multi_sz = get_persistent_devices(dev, success);
        if (multi_sz.empty()) {
                if (!success && GetLastError() == ERROR_FILE_NOT_FOUND) { // persistent_devices_value_name is absent
                        success = true; // not saved yet
                }
                return devs;
        }

        for (auto &ws: split_multi_sz(multi_sz)) {

                auto s = wchar_to_utf8(ws);
                
                if (auto d = parse_device_location(s); is_malformed(d)) {
                        libusbip::output("malformed '{}'", s);
                } else {
                        devs.push_back(std::move(d));
                }
        }

        return devs;
}
