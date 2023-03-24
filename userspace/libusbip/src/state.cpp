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

} // namespace


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
