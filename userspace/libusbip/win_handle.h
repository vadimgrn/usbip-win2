/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "generic_handle.h"

#include <cassert>
#include <windows.h>

namespace usbip
{

struct invalid_handle_traits
{
        static auto invalid() noexcept { return INVALID_HANDLE_VALUE; }
};

struct nullable_handle_traits 
{
        static HANDLE invalid() noexcept { return nullptr; }
};

struct hmodule_traits 
{
        static HMODULE invalid() noexcept { return nullptr; }
};


using Handle = generic_handle<invalid_handle_traits>;

template<>
inline void close_handle(Handle::type h, Handle::tag_type) noexcept
{
        [[maybe_unused]] auto ok = CloseHandle(h);
        assert(ok);
}


using NullableHandle = generic_handle<nullable_handle_traits>;

template<>
inline void close_handle(NullableHandle::type h, NullableHandle::tag_type) noexcept
{
        [[maybe_unused]] auto ok = CloseHandle(h);
        assert(ok);
}


using HModule = generic_handle<hmodule_traits>;

template<>
inline void close_handle(_In_ HModule::type h, _In_ HModule::tag_type) noexcept
{
        [[maybe_unused]] auto ok = FreeLibrary(h);
        assert(ok);
}

} // namespace usbip
