/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "generic_handle.h"

#include <cassert>
#include <functional>

#include <windows.h>

namespace usbip
{

struct HKeyTag {};
using HKey = generic_handle<HKEY, HKeyTag, HKEY(INVALID_HANDLE_VALUE)>;

template<>
inline void close_handle(HKey::type h, HKey::tag_type) noexcept
{
        [[maybe_unused]] auto err = RegCloseKey(h);
        assert(err == ERROR_SUCCESS);
}

} // namespace usbip


namespace std
{

template<>
struct std::hash<usbip::HKey>
{
        auto operator() (const usbip::HKey &h) const noexcept
        {
                std::hash<usbip::HKey::type> f;
                return f(h.get());
        }
};

inline void swap(usbip::HKey &a, usbip::HKey &b) noexcept
{
        a.swap(b);
}

} // namespace std
