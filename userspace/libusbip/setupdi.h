/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <windows.h>
#include <setupapi.h>

#include <cassert>
#include <memory>

#include "generic_handle.h"

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

using walkfunc_t = std::function<bool(HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data)>;
bool traverse_intfdevs(_In_ const GUID &guid, _In_ const walkfunc_t &walker);

std::shared_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> 
get_intf_detail(_In_ HDEVINFO dev_info, _In_ SP_DEVINFO_DATA *dev_info_data, _In_ const GUID &guid);

} // namespace usbip
