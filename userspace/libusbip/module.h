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

struct HModuleTag {};
using HModule = generic_handle<HMODULE, HModuleTag, nullptr>;

template<>
inline void close_handle(HModule::type h, HModule::tag_type) noexcept
{
        [[maybe_unused]] auto ok = FreeLibrary(h);
        assert(ok);
}

} // namespace usbip


namespace std
{

using usbip::HModule;

template<>
struct std::hash<HModule>
{
        auto operator() (const HModule &h) const noexcept
        {
                std::hash<HModule::type> f;
                return f(h.get());
        }
};

inline void swap(HModule &a, HModule &b) noexcept
{
        a.swap(b);
}

} // namespace std
