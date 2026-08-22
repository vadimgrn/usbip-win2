/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "..\generic_handle.h"

#include <cassert>
#include <winreg.h>

namespace usbip
{

struct hkey_traits 
{
        static HKEY invalid() noexcept { return nullptr; }
};

using HKey = generic_handle<hkey_traits>;

template<>
inline void close_handle(_In_ HKey::type h, _In_ HKey::tag_type) noexcept
{
        [[maybe_unused]] auto err = RegCloseKey(h);
        assert(err == ERROR_SUCCESS);
}

} // namespace usbip
