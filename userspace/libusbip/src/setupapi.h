/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "..\generic_handle.h"

#include <cassert>
#include <SetupAPI.h>

namespace usbip
{

struct hdevinfo_tag {};
using hdevinfo = generic_handle<HDEVINFO, hdevinfo_tag, INVALID_HANDLE_VALUE>;

template<>
inline void close_handle(_In_ hdevinfo::type h, _In_ hdevinfo::tag_type) noexcept
{
	[[maybe_unused]] auto ok = SetupDiDestroyDeviceInfoList(h);
	assert(ok);
}


struct HInfTag {};
using HInf = generic_handle<HINF, HInfTag, HINF(INVALID_HANDLE_VALUE)>;

template<>
inline void close_handle(_In_ HInf::type h, _In_ HInf::tag_type) noexcept
{
        SetupCloseInfFile(h);
}


struct HspFileQTag {};
using HspFileQ = generic_handle<HSPFILEQ, HspFileQTag, HSPFILEQ(INVALID_HANDLE_VALUE)>;

template<>
inline void close_handle(_In_ HspFileQ::type h, _In_ HspFileQ::tag_type) noexcept
{
        [[maybe_unused]] auto ok = SetupCloseFileQueue(h);
        assert(ok);
}

} // namespace usbip
