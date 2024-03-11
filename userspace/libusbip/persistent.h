/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "dllspec.h"

#include <windows.h>
#include <vector>

namespace usbip
{
        struct device_location;
}

namespace usbip::vhci
{

/**
 * Persistent devices will be attached each time the driver is loaded.
 * @param dev handle of the driver device
 * @param devices to stash
 * @return call GetLastError() if false is returned
 */
USBIP_API bool set_persistent(_In_ HANDLE dev, _In_ const std::vector<device_location> &devices);

/**
 * @param dev handle of the driver device
 * @param success call GetLastError() if false is returned
 * @return devices stashed by set_persistent()
 */
USBIP_API std::vector<device_location> get_persistent(_In_ HANDLE dev, _Out_ bool &success);

} // namespace usbip::vhci
