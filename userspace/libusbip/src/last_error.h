/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <WinSock2.h>
#include <errhandlingapi.h>

namespace usbip
{

/*
 * libusbip uses SetLastError() regardless of error origin.  
 */
struct [[nodiscard]] set_last_error
{
        DWORD error = GetLastError();

        set_last_error() noexcept = default;
        explicit set_last_error(DWORD err) noexcept : error(err) {}

        set_last_error(const set_last_error&) = delete;
        set_last_error& operator=(const set_last_error&) = delete;

        set_last_error(set_last_error&& other) noexcept 
                : error(other.error), m_active(other.m_active) 
        { 
                other.m_active = false; 
        }

        set_last_error& operator=(set_last_error&& other) noexcept {
                if (this != &other) {
                        error = other.error;
                        m_active = other.m_active;
                        other.m_active = false;
                }
                return *this;
        }

        ~set_last_error() {
                if (m_active) {
                        SetLastError(error);
                }
        }

        void dismiss() noexcept { m_active = false; }

        explicit operator bool() const noexcept { return error != 0; }
        bool operator !() const noexcept { return error == 0; }

        auto get() const noexcept { return error; }

private:
        bool m_active{true};
};

/**
 * @return scope guard capturing the last Windows Sockets error.
 */
[[nodiscard]] inline set_last_error make_wsa_last_error() noexcept
{
        return set_last_error(static_cast<DWORD>(WSAGetLastError()));
}

} // namespace usbip
