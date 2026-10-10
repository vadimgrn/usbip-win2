/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "dllspec.h"
#include "win_handle.h"
#include <sal.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#if __cplusplus >= 202002L
  #include <stop_token>
  #include <memory>
#endif

namespace usbip
{

struct isolated_volume
{
        std::wstring nt_device;     ///< NT device namespace path for the volume (e.g. \Device\HarddiskVolumeN)
        wchar_t drive_letter{};     ///< drive letter in caller's session DosDevices, or L'\0' if unassigned/failed
        unsigned long dacl_error{}; ///< Win32 error code from hardening DACL (ERROR_SUCCESS on success)
};

/**
 * Completion callback invoked on a Windows Threadpool worker thread.
 *
 * @param port roothub port number
 * @param result list of isolated volumes if successful, or std::nullopt (with GetLastError()) on error
 */
using isolate_port_volumes_callback = std::function<void(int port, std::optional<std::vector<isolated_volume>> result)>;

/**
 * Discovers and isolates storage volumes associated with a USB port.
 * Moves the drive letter from \GLOBAL?? to the caller's session DosDevices,
 * and sets the volume DACL so only SYSTEM and current session's logon SID have access.
 *
 * @param port roothub port number (1-based)
 * @param timeout_ms time to wait for volume arrival in milliseconds
 * @param cancel_event optional event handle to cancel the volume isolation early
 * @return list of isolated volumes, call GetLastError() if std::nullopt is returned
 */
USBIP_API std::optional<std::vector<isolated_volume>> isolate_port_volumes(
        _In_ int port,
        _In_ unsigned long timeout_ms = 4000,
        _In_opt_ HANDLE cancel_event = HANDLE{});

#if __cplusplus >= 202002L
/**
 * Discovers and isolates storage volumes associated with a USB port.
 *
 * @param port roothub port number (1-based)
 * @param timeout_ms time to wait for volume arrival in milliseconds
 * @param stoken stop token to cancel the volume isolation early
 * @return list of isolated volumes, call GetLastError() if std::nullopt is returned
 */
inline std::optional<std::vector<isolated_volume>> isolate_port_volumes(
        _In_ int port,
        _In_ unsigned long timeout_ms,
        _In_ std::stop_token stoken)
{
        if (stoken.stop_requested()) {
                SetLastError(ERROR_CANCELLED);
                return std::nullopt;
        }

        NullableHandle cancel_evt;
        auto on_stop = [&cancel_evt] { SetEvent(cancel_evt.get()); };

        using stop_cb_t = std::stop_callback<decltype(on_stop)>;
        std::optional<stop_cb_t> stop_cb;

        if (stoken.stop_possible()) {
                cancel_evt.reset(CreateEvent(nullptr, true, false, nullptr));
                if (!cancel_evt) {
                        return std::nullopt;
                }
                stop_cb.emplace(stoken, on_stop);
        }

        return isolate_port_volumes(port, timeout_ms, cancel_evt.get());
}
#endif // __cplusplus >= 202002L

/**
 * Asynchronously discovers and isolates storage volumes associated with a USB port
 * using the Windows Threadpool. Returns immediately without blocking the caller.
 *
 * The callback is invoked on a threadpool worker thread upon completion.
 * UI callers should marshal results back to their main event loop (e.g. wxQueueEvent or PostMessage).
 *
 * @param port roothub port number (1-based)
 * @param callback completion callback invoked on threadpool worker thread
 * @param timeout_ms time to wait for volume arrival in milliseconds
 * @param cancel_event optional event handle to cancel the volume isolation early
 * @return true if successfully queued to threadpool, call GetLastError() if false is returned
 */
USBIP_API bool isolate_port_volumes_async(
        _In_ int port,
        _In_ isolate_port_volumes_callback callback,
        _In_ unsigned long timeout_ms = 4000,
        _In_opt_ HANDLE cancel_event = HANDLE{});

inline bool isolate_port_volumes_async(
        _In_ int port,
        _In_ unsigned long timeout_ms,
        _In_ isolate_port_volumes_callback callback,
        _In_opt_ HANDLE cancel_event = HANDLE{})
{
        return isolate_port_volumes_async(port, std::move(callback), timeout_ms, cancel_event);
}

#if __cplusplus >= 202002L
/**
 * Asynchronously discovers and isolates storage volumes associated with a USB port
 * using the Windows Threadpool with a C++20 std::stop_token.
 *
 * @param port roothub port number (1-based)
 * @param callback completion callback invoked on threadpool worker thread
 * @param timeout_ms time to wait for volume arrival in milliseconds
 * @param stoken stop token to cancel the volume isolation early
 * @return true if successfully queued to threadpool, call GetLastError() if false is returned
 */
inline bool isolate_port_volumes_async(
        _In_ int port,
        _In_ isolate_port_volumes_callback callback,
        _In_ unsigned long timeout_ms,
        _In_ std::stop_token stoken)
{
        if (stoken.stop_requested()) {
                SetLastError(ERROR_CANCELLED);
                return false;
        }

        if (!stoken.stop_possible()) {
                return isolate_port_volumes_async(port, std::move(callback), timeout_ms, HANDLE{});
        }

        NullableHandle cancel_evt(CreateEvent(nullptr, true, false, nullptr));
        if (!cancel_evt) {
                return false;
        }

        struct async_stop_guard
        {
                NullableHandle evt;
                std::optional<std::stop_callback<std::function<void()>>> stop_cb;
        };

        auto guard = std::make_shared<async_stop_guard>();
        guard->evt = std::move(cancel_evt);
        guard->stop_cb.emplace(stoken, [h = guard->evt.get()] { SetEvent(h); });

        auto wrapped_cb = [callback = std::move(callback), guard](
                int port, std::optional<std::vector<isolated_volume>> res) {
                callback(port, std::move(res));
        };

        return isolate_port_volumes_async(port, std::move(wrapped_cb), timeout_ms, guard->evt.get());
}

inline bool isolate_port_volumes_async(
        _In_ int port,
        _In_ unsigned long timeout_ms,
        _In_ isolate_port_volumes_callback callback,
        _In_ std::stop_token stoken)
{
        return isolate_port_volumes_async(port, std::move(callback), timeout_ms, stoken);
}
#endif // __cplusplus >= 202002L

/**
 * Removes session-isolated drive letters associated with a USB port.
 *
 * @param port roothub port number (1-based)
 * @return call GetLastError() if false is returned
 */
USBIP_API bool cleanup_port_volumes(_In_ int port);

} // namespace usbip
