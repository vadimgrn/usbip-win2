/*
 * Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <optional>
#include <stop_token>
#include <windows.h>

namespace usbip
{

/**
 * RAII guard that installs a console control handler (SetConsoleCtrlHandler)
 * to gracefully cancel long-running operations upon receiving Ctrl+C or Ctrl+Break.
 *
 * Supports cancelling either:
 * - Cooperative operations via std::stop_token (e.g. connect).
 * - Pending driver I/O on the VHCI device handle via vhci::cancel_io (e.g. attach, detach).
 */
class ctrl_c_guard
{
public:
        /**
         * Initializes an internal std::stop_source and registers the console handler.
         * Use token() to pass the cancellation token to cancellable functions.
         */
        ctrl_c_guard() noexcept;

        /**
         * Registers the console handler to cancel pending I/O on the given VHCI device handle.
         * @param dev handle of the VHCI driver device
         */
        explicit ctrl_c_guard(HANDLE dev) noexcept;

        /**
         * Registers the console handler to request stop on an external std::stop_source.
         * @param ssrc reference to the stop_source to request stop on
         */
        explicit ctrl_c_guard(std::stop_source &ssrc) noexcept;

        /**
         * Unregisters the console control handler and clears active cancellation targets.
         */
        ~ctrl_c_guard() noexcept;

        ctrl_c_guard(const ctrl_c_guard&) = delete;
        ctrl_c_guard& operator=(const ctrl_c_guard&) = delete;

        /**
         * @return std::stop_token associated with this guard
         */
        std::stop_token token() const noexcept;
        operator std::stop_token() const noexcept { return token(); }

private:
        std::optional<std::stop_source> m_ssrc;

        static BOOL WINAPI handler(DWORD ctrl_type) noexcept;
};

} // namespace usbip
