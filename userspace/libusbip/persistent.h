/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "dllspec.h"

#include <vector>
#include <optional>

using HANDLE = void*; // windows.h

namespace usbip
{
        struct device_config;
}

namespace usbip::vhci
{

/**
 * Persistent devices will be attached each time the driver is loaded.
 * @param dev handle of the driver device
 * @param devices to stash
 * @return call GetLastError() if false is returned
 */
USBIP_API bool set_persistent(_In_ HANDLE dev, _In_ const std::vector<device_config> &devices);

/**
 * @param dev handle of the driver device
 * @return devices stashed by set_persistent() if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<device_config>> get_persistent(_In_ HANDLE dev);

/**
 * Sets persistent devices using a temporary driver handle.
 * @param devices to stash
 * @return call GetLastError() if false is returned
 */
USBIP_API bool set_persistent(_In_ const std::vector<device_config> &devices);

/**
 * Gets persistent devices using a temporary driver handle.
 * @return devices stashed by set_persistent() if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<device_config>> get_persistent();

} // namespace usbip::vhci
