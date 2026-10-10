/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "../persistent.h"
#include "../vhci.h"

#include "sid.h"
#include "output.h"

#include <usbip/vhci.h>
#include <ranges>

namespace
{

using namespace usbip;

auto is_malformed(_In_ const device_location &d) noexcept
{
        return d.hostname.empty() || d.service.empty() || d.busid.empty();
}

auto is_malformed_isolation(_In_ const device_config &d, _In_ bool check_user_sid) noexcept
{
        switch (d.iso_mode) {
        case isolation::session:
                return true; // cannot be used for persistent device
        case isolation::user:
                return check_user_sid && d.user_sid.empty();
        case isolation::none:
        default:
                return !d.user_sid.empty();
        }
}

auto is_malformed(_In_ const device_config &d, _In_ bool check_user_sid = true) noexcept
{
        return  is_malformed(d.location) ||
                is_malformed_isolation(d, check_user_sid) || 
                !validate_device_serial(d.serial);
}

auto config_to_str(_In_ const device_config &dev, _In_ const std::optional<sid_blob> &user_sid)
{
        auto &dl = dev.location;
        auto s = std::format("host={};port={};busid={}", dl.hostname, dl.service, dl.busid);

        if (!dev.serial.empty()) {
                s += std::format(";serial={}", dev.serial);
        }

        if (dev.recv_mode == receive_mode::low_latency) {
                static_assert(!static_cast<int>(receive_mode::zero_copy)); // is a default
                s += ";recv_mode=low-latency";
        }

        if (user_sid) {
                s += std::format(";isolate=user;user_sid={}", to_string_hex(*user_sid));
        }

        return s;
}

std::expected<std::wstring, DWORD> devices_to_multi_sz(_In_ const std::vector<device_config> &devices)
{
        std::wstring multi_sz;

        for (auto &dev: devices) {

                std::optional<sid_blob> user_sid;

                if (dev.iso_mode == isolation::user) {
                        auto res = !dev.user_sid.empty() ? parse_sid(dev.user_sid) : get_user_sid(); 
                        if (!res) {
                                libusbip::output("invalid device_config.user_sid='{}'", dev.user_sid);
                                return std::unexpected(res.error());
                        }
                        user_sid = *res;
                }

                auto &dl = dev.location;
                auto wsk_events = dev.recv_mode == receive_mode::low_latency;

                if (is_malformed(dev, false)) { // empty dev.user_sid is OK
                        libusbip::output("malformed device_config( hostname='{}', port='{}', "
                                         "busid='{}', serial='{}', wsk_events={}, iso_mode={}, user_sid='{}' )",
                                         dl.hostname, dl.service, dl.busid, dev.serial, wsk_events,
                                         static_cast<int>(dev.iso_mode), dev.user_sid);

                        return std::unexpected(ERROR_INVALID_PARAMETER);
                }

                auto s = config_to_str(dev, user_sid);

                if (auto ws = utf8_to_wchar(s)) {
                        *ws += L'\0';
                        multi_sz += *ws;
                } else {
                        libusbip::output("utf8_to_wchar('{}') error {}", s, ws.error());
                        return std::unexpected(ws.error());
                }
        }

        multi_sz += L'\0';
        if (devices.empty()) { // double null terminator if empty
                multi_sz += L'\0';
        }

        return multi_sz;
}

auto parse_recv_mode(_Inout_ receive_mode &mode, _In_ std::wstring_view val) noexcept
{
        if (equal_ordinal(val, L"low-latency", true)) {
                mode = receive_mode::low_latency;
                return true;
        }

        if (equal_ordinal(val, L"zero-copy", true)) {
                mode = receive_mode::zero_copy;
                return true;
        }

        return false;
}

auto parse_isolation_mode(_Inout_ isolation &mode, _In_ std::wstring_view val) noexcept
{
        if (equal_ordinal(val, L"none", true)) {
                mode = isolation::none;
                return true;
        }

        if (equal_ordinal(val, L"user", true)) {
                mode = isolation::user;
                return true;
        }

        return false;
}

auto assign(_Inout_ std::string &dst, _In_ std::wstring_view src)
{
        if (auto s = wchar_to_utf8(src)) {
                dst = std::move(*s);
                return true;
        }
        return false;
}

auto parse_user_sid(_Inout_ std::string &user_sid, _In_ std::wstring_view val)
{
        user_sid.clear();

        if (auto sid = parse_sid_hex(val)) {
                user_sid = to_string(*sid);
        }

        return !user_sid.empty();
}

auto parse_token(_Inout_ device_config &dev, _In_ std::wstring_view token)
{
        auto eq = token.find(L'=');
        if (eq == std::wstring_view::npos) {
                return false;
        }

        auto key = token.substr(0, eq);
        auto val = token.substr(eq + 1);

        if (key.empty() || val.empty()) {
                return false;
        }

        if (equal_ordinal(key, L"host", true)) {
                return assign(dev.location.hostname, val);
        } else if (equal_ordinal(key, L"port", true)) {
                return assign(dev.location.service, val);
        } else if (equal_ordinal(key, L"busid", true)) {
                return assign(dev.location.busid, val);
        } else if (equal_ordinal(key, L"serial", true)) {
                return assign(dev.serial, val);
        } else if (equal_ordinal(key, L"recv_mode", true)) {
                return parse_recv_mode(dev.recv_mode, val);
        } else if (equal_ordinal(key, L"isolate", true)) {
                return parse_isolation_mode(dev.iso_mode, val);
        } else if (equal_ordinal(key, L"user_sid", true)) {
                return parse_user_sid(dev.user_sid, val);
        }

        return true; // unknown keys are ignored for forward compatibility
}

/*
 * Format: host=<val>;port=<val>;busid=<val>[;serial=<val>][;recv_mode=<val>][;isolate=<val>][;user_sid=<val>]
 * Designed for forward compatibility:
 * - Keys can appear in any order.
 * - Future entries with unknown keys are accepted; only known keys are parsed and unknown keys are ignored.
 */
std::optional<device_config> parse_device_config(_In_ std::wstring_view str)
{
        std::optional<device_config> result(std::in_place);

        for (const auto part : str | std::views::split(L';')) {

                std::wstring_view token(part.begin(), part.end());
                if (token.empty()) {
                        continue;
                }

                if (!parse_token(*result, token)) {
                        return std::nullopt;
                }
        }

        if (is_malformed(*result)) {
                result.reset();
        }

        return result;
}

std::expected<std::wstring, DWORD> get_persistent_devices(_In_ HANDLE dev)
{
        auto err = ERROR_SUCCESS;
        std::wstring val(512, L'\0');

        for (int attempt = 0; attempt < 3; ++attempt) {

                auto bytes = std::span(val).size_bytes();

                DWORD BytesReturned{}; // must be set if the last arg is NULL
                auto ok = DeviceIoControl(dev, vhci::ioctl::GET_PERSISTENT, nullptr, 0, 
                                          val.data(), static_cast<DWORD>(bytes), &BytesReturned, nullptr);

                auto cnt = BytesReturned/sizeof(val.front());

                if (ok) {
                        val.resize(cnt);
                        return val;
                }

                err = GetLastError();

                if (err == ERROR_MORE_DATA && BytesReturned > bytes) { // WdfRegistryQueryValue -> STATUS_BUFFER_OVERFLOW
                        val.resize(cnt);
                } else {
                        break;
                }
        }

        return std::unexpected(err);
}

} // namespace


bool usbip::vhci::set_persistent(_In_ HANDLE dev, _In_ const std::vector<device_config> &devices)
{
        auto res = devices_to_multi_sz(devices);
        if (!res) {
                SetLastError(res.error());
                return false;
        }

        auto bytes = std::span(*res).size_bytes();
        DWORD BytesReturned{}; // must be set if the last arg is NULL

        auto ok = DeviceIoControl(dev, ioctl::SET_PERSISTENT, res->data(), static_cast<DWORD>(bytes),
                                  nullptr, 0, &BytesReturned, nullptr);

        assert(!BytesReturned);
        return ok;
}

auto usbip::vhci::get_persistent(_In_ HANDLE dev) -> std::optional<std::vector<device_config>>
{
        std::optional<std::vector<device_config>> devs;

        auto res = get_persistent_devices(dev);
        if (!res) {
                if (res.error() == ERROR_FILE_NOT_FOUND) { // persistent_devices_value_name is absent
                        devs.emplace(); // not an error
                } else {
                        SetLastError(res.error());
                }
                return devs;
        }

        auto strings = split_multi_sz(*res);

        devs.emplace();
        devs->reserve(strings.size());

        for (auto &ws: strings) {
                if (auto dc = parse_device_config(ws)) {
                        devs->push_back(std::move(*dc));
                } else {
                        libusbip::output("invalid '{}'", wchar_to_utf8_or(ws));
                }
        }

        return devs;
}
