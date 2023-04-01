/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "..\persistent.h"
#include "..\vhci.h"
#include "output.h"

#include <usbip\vhci.h>
#include <ranges>

namespace
{

using namespace usbip;

auto &parameters_key_name = L"\\Parameters"; // @see WdfDriverOpenParametersRegistryKey

/*
 * @see Registry Key Object Routines 
 */
auto make_key_path(_In_ std::wstring_view abspath)
{
        std::pair<HKEY, std::wstring> result(HKEY(), abspath);
        auto &path = result.second;

        std::wstring_view prefix(L"\\REGISTRY\\MACHINE\\");

        if (auto cch = DWORD(prefix.size()); path.size() >= cch) {
                if (auto n = CharUpperBuff(path.data(), cch); n != cch) {
                        libusbip::output(L"CharUpperBuff('{}', {}) -> {}", path, cch, n);
                        path = abspath; // revert
                }
        }

        if (path.starts_with(prefix)) {
                result.first = HKEY_LOCAL_MACHINE;
                abspath.remove_prefix(prefix.size());
        }

        path = abspath;
        return result;
}

auto driver_registry_path(_In_ HANDLE dev)
{
        using namespace vhci;
        std::pair<HKEY, std::wstring> result;

        const auto path_offset = offsetof(ioctl::driver_registry_path, path);
        ioctl::driver_registry_path r{{ .size = sizeof(r) }};

        if (DWORD BytesReturned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::DRIVER_REGISTRY_PATH, 
                            &r, path_offset, &r, sizeof(r), &BytesReturned, nullptr)) {

                std::wstring_view abspath(r.path, (BytesReturned - path_offset)/sizeof(*r.path));
                result = make_key_path(abspath);
        }

        return result;
}

auto is_malformed(_In_ const device_location &d)
{
        return d.hostname.empty() || d.service.empty() || d.busid.empty();
}

auto make_multi_sz(_Out_ std::wstring &result, _In_ const std::vector<device_location> &v)
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

auto get_persistant_devices(_In_ HANDLE dev, _Out_ bool &success)
{
        success = false;
        std::wstring multi_sz;

        auto [key, subkey] = driver_registry_path(dev);
        if (subkey.empty()) {
                return multi_sz;
        }

        subkey += parameters_key_name;

        for (DWORD bytes = 1024, stop = false; ; ) {

                multi_sz.resize(bytes/sizeof(multi_sz[0]));
                if (stop) {
                        break;
                }

                auto err = RegGetValue(key, subkey.c_str(), persistent_devices_value_name, 
                                       RRF_RT_REG_MULTI_SZ, nullptr, multi_sz.data(), &bytes);

                if (err == ERROR_MORE_DATA) {
                        // continue;
                } else if (stop = true, success = !err; !success) {
                        libusbip::output(L"RegGetValue('{}', value_name='{}') error {:#x}",
                                         subkey, persistent_devices_value_name, err);

                        SetLastError(err);
                        bytes = 0; // clear result
                }
        }

        return multi_sz;
}

} // namespace


bool usbip::vhci::set_persistent(_In_ HANDLE dev, _In_ const std::vector<device_location> &devices)
{
        std::wstring value;
        if (auto err = ::make_multi_sz(value, devices)) {
                SetLastError(err);
                return false;
        }

        auto [key, subkey] = driver_registry_path(dev);
        if (subkey.empty()) {
                return false;
        }

        subkey += parameters_key_name;

        auto err = RegSetKeyValue(key, subkey.c_str(), persistent_devices_value_name, 
                                  REG_MULTI_SZ, value.data(), DWORD(value.size()*sizeof(value[0])));

        if (err) {
                libusbip::output(L"RegSetKeyValue('{}', value_name='{}') error {:#x}", 
                                 subkey, persistent_devices_value_name, err);

                SetLastError(err);
        }

        return !err;
}

auto usbip::vhci::get_persistent(_In_ HANDLE dev, _Out_ bool &success) -> std::vector<device_location>
{
        std::vector<device_location> devs;

        auto multi_sz = get_persistant_devices(dev, success);
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
