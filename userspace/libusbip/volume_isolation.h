/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "dllspec.h"

#include <string>
#include <vector>
#include <optional>

#if __cplusplus >= 202002L
  #include <stop_token>
#endif

using HANDLE = void*; // windows.h

namespace usbip
{

struct isolated_volume
{
        std::wstring nt_device;     ///< NT device namespace path for the volume (e.g. \Device\HarddiskVolumeN)
        wchar_t drive_letter{};     ///< drive letter in caller's session DosDevices, or L'\0' if unassigned/failed
        unsigned long dacl_error{}; ///< Win32 error code from hardening DACL (ERROR_SUCCESS on success)
};

/**
 * Discovers and isolates storage volumes associated with a USB port.
 * Moves the drive letter from \GLOBAL?? to the caller's session DosDevices,
 * and sets the volume DACL so only SYSTEM and current session's logon SID have access.
 *
 * @param port roothub port number (1-based)
 * @param timeout_ms time to wait for volume arrival in milliseconds
 * @param cancel_event optional manual-reset event handle to cancel the volume isolation early
 *        (use create_cancel_event() to create it)
 * @return list of isolated volumes, call GetLastError() if std::nullopt is returned
 *
 * @note When to call:
 *       - Call immediately after vhci::attach() succeeds with a valid port number (port > 0).
 *       - Only call when attaching with isolation enabled (config.iso_mode != isolation::none).
 *         If attached with isolation::none, the volumes are intended to be shared globally.
 *       - Intended for storage class devices (e.g. flash drives, external disks). Non-storage
 *         devices quickly bail out once their PnP class is determined, but calling is unnecessary.
 *       - In GUI or interactive applications, run on a background worker thread with a cancel
 *         event or stop_token so the UI remains responsive during asynchronous PnP enumeration.
 *       - Privileged isolation (removing the global mount point from \GLOBAL?? and hardening
 *         the volume and root directory DACLs) is performed in kernel mode by the VHCI
 *         driver (alongside automatic volume arrival DACL hardening via PnP notifications).
 *         Standard non-admin users therefore obtain full isolation without elevation, while
 *         this function maps the session-local drive letter in the caller's interactive session.
 *
 * @see create_cancel_event
 * @see vhci::attach
 */
USBIP_API std::optional<std::vector<isolated_volume>> isolate_port_volumes(
        _In_ int port,
        _In_ unsigned long timeout_ms = 4000,
        _In_opt_ HANDLE cancel_event = HANDLE{});

/**
 * Removes session-isolated drive letters associated with a USB port.
 *
 * @param port roothub port number (1-based)
 * @return call GetLastError() if false is returned
 * @note Automatically called by vhci::detach() before hardware plugout to ensure
 *       session DosDevices links are removed while the DevNode hierarchy is still intact.
 */
USBIP_API bool cleanup_port_volumes(_In_ int port);


#if __cplusplus >= 202002L

/**
 * Discovers and isolates storage volumes associated with a USB port.
 *
 * @param port roothub port number (1-based)
 * @param timeout_ms time to wait for volume arrival in milliseconds
 * @param stoken stop token to cancel the volume isolation early
 * @return list of isolated volumes, call GetLastError() if std::nullopt is returned
 *
 * @note When to call:
 *       - Call immediately after vhci::attach() succeeds with a valid port number (port > 0)
 *         when config.iso_mode != isolation::none on storage class devices.
 *       - Run on a background worker thread; the stop_token allows the caller to abort the wait.
 * @see vhci::attach
 */
USBIP_API std::optional<std::vector<isolated_volume>> isolate_port_volumes(
        _In_ int port, _In_ unsigned long timeout_ms, _In_ std::stop_token stoken);

#endif // __cplusplus >= 202002L

} // namespace usbip
