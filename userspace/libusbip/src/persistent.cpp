/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "../persistent.h"
#include "../vhci.h"
#include "../win_handle.h"
#include "output.h"
#include "strconv.h"

#include <usbip/vhci.h>
#include <usbip/hex.h>
#include <sddl.h>

#include <ranges>
#include <span>

namespace
{

using namespace usbip;

auto is_malformed(_In_ const device_location &d) noexcept
{
        return d.hostname.empty() || d.service.empty() || d.busid.empty();
}

auto is_malformed(_In_ const device_config &d) noexcept
{
        return  is_malformed(d.location) ||
                !validate_device_serial(d.serial) ||
                d.iso_mode == isolation::session ||
                (d.iso_mode == isolation::user && d.owner_sid.empty()) ||
                (d.iso_mode != isolation::user && !d.owner_sid.empty());
}

std::string get_current_user_sid()
{
        HANDLE token_raw{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_raw)) {
                return {};
        }
        Handle token(token_raw);

        DWORD user_sz{};
        GetTokenInformation(token.get(), TokenUser, nullptr, 0, &user_sz);
        if (user_sz) {
                std::vector<BYTE> user_buf(user_sz);
                if (GetTokenInformation(token.get(), TokenUser, user_buf.data(), user_sz, &user_sz)) {
                        auto tu = reinterpret_cast<TOKEN_USER*>(user_buf.data());
                        PSTR sid_str{};
                        if (ConvertSidToStringSidA(tu->User.Sid, &sid_str)) {
                                std::string res = sid_str;
                                LocalFree(sid_str);
                                return res;
                        }
                }
        }
        return {};
}

std::string sid_bytes_to_hex(_In_ PSID sid)
{
        auto len = GetLengthSid(sid);
        auto bytes = static_cast<const unsigned char*>(sid);
        std::string hex;
        hex.reserve(len * 2);
        for (DWORD i = 0; i < len; ++i) {
                std::format_to(std::back_inserter(hex), "{:02x}", bytes[i]);
        }
        return hex;
}

std::optional<std::string> hex_to_sddl(_In_ std::wstring_view hex)
{
        if (hex.size() % 2 || hex.size() < 2*(sizeof(SID) - sizeof(ULONG)) || 
            hex.size() > 2*SECURITY_MAX_SID_SIZE) {
                return std::nullopt;
        }

        std::vector<unsigned char> bytes;
        bytes.resize(hex.size()/2);

        for (size_t i = 0; i < hex.size(); i += 2) {
                auto hi = from_hex(hex[i]);
                auto lo = from_hex(hex[i + 1]);
                if (hi < 0 || lo < 0) {
                        return std::nullopt;
                }
                bytes[i] = static_cast<unsigned char>((hi << 4) | lo);
        }

        auto sid = reinterpret_cast<PSID>(bytes.data());
        if (!(IsValidSid(sid) && GetLengthSid(sid) == bytes.size())) {
                return std::nullopt;
        }

        if (PSTR sid_str{}; ConvertSidToStringSidA(sid, &sid_str)) {
                std::string res = sid_str;
                LocalFree(sid_str);
                return res;
        }

        return std::nullopt;
}

std::optional<std::string> sid_to_hex(_In_ const std::string &owner_sid)
{
        if (owner_sid.starts_with("S-") || owner_sid.starts_with("s-")) {
                PSID sid{};
                if (ConvertStringSidToSidA(owner_sid.c_str(), &sid)) {
                        auto hex = sid_bytes_to_hex(sid);
                        LocalFree(sid);
                        return hex;
                }
                return std::nullopt;
        }

        if (auto ws = utf8_to_wchar(owner_sid); ws && hex_to_sddl(*ws)) {
                return owner_sid;
        }
        return std::nullopt;
}

std::expected<std::wstring, DWORD> devices_to_multi_sz(_In_ const std::vector<device_config> &devices)
{
        std::wstring multi_sz;

        for (auto dev_copy: devices) {
                if (dev_copy.iso_mode == isolation::user && dev_copy.owner_sid.empty()) {
                        dev_copy.owner_sid = get_current_user_sid();
                }

                auto &dl = dev_copy.location;
                auto wsk_events = dev_copy.recv_mode == receive_mode::low_latency;

                if (is_malformed(dev_copy)) {
                        libusbip::output("malformed device_config( hostname='{}', port='{}', "
                                         "busid='{}', serial='{}', wsk_events={}, iso_mode={}, owner_sid='{}' )",
                                         dl.hostname, dl.service, dl.busid, dev_copy.serial, wsk_events,
                                         static_cast<int>(dev_copy.iso_mode), dev_copy.owner_sid);

                        return std::unexpected(ERROR_INVALID_PARAMETER);
                }

                auto s = std::format("host={};port={};busid={}", dl.hostname, dl.service, dl.busid);

                if (!dev_copy.serial.empty()) {
                        s += std::format(";serial={}", dev_copy.serial);
                }

                if (wsk_events) {
                        s += ";recv_mode=low-latency";
                }
                if (dev_copy.iso_mode == isolation::user) {
                        auto hex = sid_to_hex(dev_copy.owner_sid);
                        if (!hex) {
                                libusbip::output("failed to convert owner SID '{}' to hex", dev_copy.owner_sid);
                                return std::unexpected(ERROR_INVALID_PARAMETER);
                        }
                        s += std::format(";isolate=user;owner={}", *hex);
                }

                if (auto ws = utf8_to_wchar(s)) {
                        *ws += L'\0';
                        multi_sz += *ws;
                } else {
                        libusbip::output("utf8_to_wchar('{}') error {}", s, ws.error());
                        return std::unexpected(ERROR_INVALID_PARAMETER);
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

auto parse_owner_sid(_Inout_ std::string &owner_sid, _In_ std::wstring_view val)
{
        if (val.starts_with(L"S-") || val.starts_with(L"s-")) {
                return assign(owner_sid, val);
        }
        if (auto sddl = hex_to_sddl(val)) {
                owner_sid = std::move(*sddl);
                return true;
        }
        return false;
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
        } else if (equal_ordinal(key, L"owner", true)) {
                return parse_owner_sid(dev.owner_sid, val);
        }

        return true; // unknown keys are ignored for forward compatibility
}

/*
 * Format: host=<val>;port=<val>;busid=<val>[;serial=<val>][;recv_mode=<val>][;isolate=<val>][;owner=<val>]
 * Designed for forward compatibility:
 * - Keys can appear in any order.
 * - Future entries with unknown keys are accepted; only known keys are parsed and unknown keys are ignored.
 */
auto parse_device_config(_In_ std::wstring_view str) -> std::optional<device_config>
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

        return is_malformed(*result) ? std::nullopt : result;
}

auto get_persistent_devices(_In_ HANDLE dev)
{
        std::optional<std::wstring> val(std::in_place, 512, L'\0');
        constexpr int max_attempts = 3;

        for (int attempt = 0; attempt < max_attempts; ++attempt) {

                auto bytes = std::span(*val).size_bytes();

                DWORD BytesReturned{}; // must be set if the last arg is NULL
                auto ok = DeviceIoControl(dev, vhci::ioctl::GET_PERSISTENT, nullptr, 0, 
                                          val->data(), static_cast<DWORD>(bytes), &BytesReturned, nullptr);

                auto cnt = BytesReturned/sizeof(val->front());

                if (ok) {
                        val->resize(cnt);
                        return val;
                }

                if (GetLastError() == ERROR_MORE_DATA && BytesReturned > bytes) { // WdfRegistryQueryValue -> STATUS_BUFFER_OVERFLOW
                        val->resize(cnt);
                } else {
                        break;
                }
        }

        val.reset();
        return val;
}

} // namespace


bool usbip::vhci::set_persistent(_In_ HANDLE dev, _In_ const std::vector<device_config> &devices)
{
        auto val = devices_to_multi_sz(devices);
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

auto usbip::vhci::get_persistent(_In_ HANDLE dev) -> std::optional<std::vector<device_config>>
{
        std::optional<std::vector<device_config>> devs;

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
                if (auto d = parse_device_config(ws); d && !is_malformed(*d)) {
                        devs->push_back(std::move(*d));
                } else {
                        libusbip::output("invalid '{}'", wchar_to_utf8_or(ws));
                }
        }

        return devs;
}
