/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "dllspec.h"
#include <windows.h>
#include <string>
#include <vector>

namespace usbip
{

struct isolated_volume
{
        std::wstring volume_device;
        wchar_t drive_letter{};
        bool dacl_hardened{};
};

/**
 * Discovers and isolates storage volumes associated with a USB port.
 * Moves the drive letter from \GLOBAL?? to the caller's session DosDevices,
 * and sets the volume DACL so only SYSTEM and current session's logon SID have access.
 *
 * @param port roothub port number (1-based)
 * @param timeout_ms time to wait for volume arrival in milliseconds
 * @return list of isolated volumes
 */
USBIP_API std::vector<isolated_volume> isolate_port_volumes(_In_ int port, _In_ DWORD timeout_ms = 4000);

/**
 * Removes session-isolated drive letters associated with a USB port.
 *
 * @param port roothub port number (1-based)
 * @return true on success
 */
USBIP_API bool cleanup_port_volumes(_In_ int port);

} // namespace usbip
