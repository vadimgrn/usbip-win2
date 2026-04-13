/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "..\persistent.h"
#include "..\vhci.h"
#include "output.h"

#include <usbip\vhci.h>
#include <ranges>
#include <span>

namespace
{

using namespace usbip;

auto is_malformed(_In_ const device_location &d)
{
        return d.hostname.empty() || d.service.empty() || d.busid.empty();
}

std::expected<std::wstring, DWORD> make_multi_sz(_In_ const std::vector<device_location> &v)
{
        std::wstring multi_sz;

        for (auto &i: v) {
                if (is_malformed(i)) {
                        libusbip::output("malformed device_location{ hostname='{}', service='{}', busid='{}' }", 
                                          i.hostname, i.service, i.busid);

                        return std::unexpected(ERROR_INVALID_PARAMETER);
                }

                if (auto s = std::format("{},{},{}", i.hostname, i.service, i.busid);
                    auto ws = utf8_to_wchar(s)) {
                        *ws += L'\0';
                        multi_sz += *ws;
                } else {
                        libusbip::output("utf8_to_wchar('{}') error {}", s, ws.error());
                        return std::unexpected(ERROR_INVALID_PARAMETER);
                }
        }

        for (int i = 0; i < v.empty() + 1; ++i) { // double null terminator if empty
                multi_sz += L'\0';
        }

        return multi_sz;
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

auto get_persistent_devices(_In_ HANDLE dev)
{
        std::optional<std::wstring> val(std::in_place, 512, L'\0');

        for (DWORD BytesReturned{}; ; ) { // must be set if the last arg is NULL

                auto bytes = std::span(*val).size_bytes();

                auto ok = DeviceIoControl(dev, vhci::ioctl::GET_PERSISTENT, nullptr, 0, 
                                          val->data(), static_cast<DWORD>(bytes), &BytesReturned, nullptr);

                if (ok || GetLastError() == ERROR_MORE_DATA) { // WdfRegistryQueryValue -> STATUS_BUFFER_OVERFLOW
                        val->resize(BytesReturned/sizeof(val->front()));
                        if (ok) {
                                break;
                        }
                } else {
                        val.reset();
                        break;
                }
        }

        return val;
}

} // namespace


bool usbip::vhci::set_persistent(_In_ HANDLE dev, _In_ const std::vector<device_location> &devices)
{
        auto val = ::make_multi_sz(devices);
        if (!val) {
                SetLastError(val.error());
                return false;
        }

        auto bytes = std::span(*val).size_bytes();
        DWORD BytesReturned{}; // must be set if the last arg is NULL

        auto ok = DeviceIoControl(dev, ioctl::SET_PERSISTENT, val->data(), static_cast<DWORD>(bytes),
                                  nullptr, 0, &BytesReturned, nullptr);

        assert(!BytesReturned);
        return ok;
}

auto usbip::vhci::get_persistent(_In_ HANDLE dev) -> std::optional<std::vector<device_location>>
{
        std::optional<std::vector<device_location>> devs;

        auto multi_sz = get_persistent_devices(dev);
        if (!multi_sz) {
                if (GetLastError() == ERROR_FILE_NOT_FOUND) { // persistent_devices_value_name is absent
                        devs.emplace(); // not an error
                }
                return devs;
        }

        auto strings = split_multi_sz(*multi_sz);

        devs.emplace();
        devs->reserve(strings.size());

        for (auto &ws: strings) {
                if (auto s = wchar_to_utf8(ws); !s) {
                        libusbip::output("wchar_to_utf8 error {}", s.error());
                } else if (auto d = parse_device_location(*s); is_malformed(d)) {
                        libusbip::output("malformed '{}'", *s);
                } else {
                        devs->push_back(std::move(d));
                }
        }

        return devs;
}
