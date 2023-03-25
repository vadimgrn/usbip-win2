/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "state.h"
#include "output.h"
#include "strconv.h"

#include "..\vhci.h"
#include "..\format_message.h"

#include <usbip\consts.h>

namespace
{

using namespace usbip;

auto get_subkey()
{
        return std::format(L"SYSTEM\\CurrentControlSet\\Services\\{}\\Parameters", driver_filename);
}

auto make_multi_sz(_In_ const std::vector<imported_device> &devices)
{
        std::wstring ws;

        for (auto &i: devices) {
                auto &r = i.location;
                auto s = r.hostname + ',' + r.service + ',' + r.busid + '\0';
                ws += utf8_to_wchar(s);
        }

        ws += L'\0';
        return ws;
}

} // namespace


bool usbip::save_imported_devices(_In_ const std::vector<imported_device> &devices)
{
        auto subkey = get_subkey();
        auto value = ::make_multi_sz(devices);

        auto err = RegSetKeyValue(HKEY_LOCAL_MACHINE, subkey.c_str(), imported_devices_value_name, 
                                  REG_MULTI_SZ, value.data(), DWORD(value.size()*sizeof(value[0])));

        if (err) {
                libusbip::output(L"RegSetKeyValue('HKLM\\{}', value_name='{}') error {:#x} {}", 
                                 subkey, imported_devices_value_name, err, wformat_message(err));

                SetLastError(err);
        }

        return !err;
}

auto usbip::load_imported_devices(_Out_ bool &success) -> std::vector<device_location>
{
        auto subkey = get_subkey();
        DWORD len{};

        auto query = [&subkey, &len](_Out_opt_ void *data)
        {
                auto err = RegGetValue(HKEY_LOCAL_MACHINE, subkey.c_str(), imported_devices_value_name, 
                                       RRF_RT_REG_MULTI_SZ, nullptr, data, &len);

                if (err) {
                        libusbip::output(L"RegGetValue('HKLM\\{}', value_name='{}') error {:#x} {}", 
                                         subkey, imported_devices_value_name, err, wformat_message(err));

                        SetLastError(err);
                }

                return err;
        };

        success = false;
        std::vector<device_location> devs;

        if (query(nullptr)) {
                return devs;
        }

        std::wstring multi_sz(len, L'\0');

        if (query(multi_sz.data())) {
                return devs;
        }

        for (auto &line: split_multi_sz(multi_sz)) {
                device_location dl{ .hostname = wchar_to_utf8(line) };
                devs.push_back(std::move(dl));
        }

        success = true;
        return devs;
}
