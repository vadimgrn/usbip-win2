/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "generic_handle.h"

#include <cassert>
#include <windows.h>

namespace usbip
{

struct HModuleTag {};
using HModule = generic_handle<HMODULE, HModuleTag, nullptr>;

template<>
inline void close_handle(_In_ HModule::type h, _In_ HModule::tag_type) noexcept
{
        [[maybe_unused]] auto ok = FreeLibrary(h);
        assert(ok);
}

} // namespace usbip
