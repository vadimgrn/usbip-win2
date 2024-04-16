/*
* Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include <errhandlingapi.h>
#include <WinSock2.h>

namespace usbip
{

/*
 * libusbip uses SetLastError() regardless of error origin.  
 */
struct set_last_error
{
        set_last_error() noexcept = default;
        explicit set_last_error(int err) noexcept : error(err) {} // WSA, etc.

        ~set_last_error() { SetLastError(error); }

        explicit operator bool() const noexcept { return !error; }
        auto operator !() const noexcept { return bool(error); }

        auto get() const noexcept { return error; }
        int error = GetLastError();
};

struct wsa_set_last_error : set_last_error
{
        wsa_set_last_error() : set_last_error(WSAGetLastError()) {}
};

} // namespace usbip
