/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "../generic_handle.h"

#include <cassert>
#include <SetupAPI.h>

namespace usbip
{

struct hdevinfo_traits 
{
        static HDEVINFO invalid() noexcept { return INVALID_HANDLE_VALUE; }
};

struct hinf_traits 
{
        static HINF invalid() noexcept { return INVALID_HANDLE_VALUE; }
};

struct hspfileq_traits 
{
        static HSPFILEQ invalid() noexcept { return INVALID_HANDLE_VALUE; }
};


using hdevinfo = generic_handle<hdevinfo_traits>;

template<>
inline void close_handle(_In_ hdevinfo::type h, _In_ hdevinfo::tag_type) noexcept
{
	[[maybe_unused]] auto ok = SetupDiDestroyDeviceInfoList(h);
	assert(ok);
}


using HInf = generic_handle<hinf_traits>;

template<>
inline void close_handle(_In_ HInf::type h, _In_ HInf::tag_type) noexcept
{
        SetupCloseInfFile(h);
}


using HspFileQ = generic_handle<hspfileq_traits>;

template<>
inline void close_handle(_In_ HspFileQ::type h, _In_ HspFileQ::tag_type) noexcept
{
        [[maybe_unused]] auto ok = SetupCloseFileQueue(h);
        assert(ok);
}

} // namespace usbip
