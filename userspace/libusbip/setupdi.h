#pragma once

#include <windows.h>
#include <setupapi.h>

#include <cassert>
#include <functional>

#include "generic_handle.h"

namespace usbip
{

struct hdevinfo_tag {};
using hdevinfo = generic_handle<HDEVINFO, hdevinfo_tag, INVALID_HANDLE_VALUE>;

template<>
inline void close_handle(hdevinfo::type h, hdevinfo::tag_type) noexcept
{
	[[maybe_unused]] auto ok = SetupDiDestroyDeviceInfoList(h);
	assert(ok);
}

using walkfunc_t = std::function<int(HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data)>;
int traverse_intfdevs(const GUID &guid, const walkfunc_t &walker);

std::shared_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> 
get_intf_detail(HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data, const GUID &pguid);

inline auto GetClassDevsW(
	_In_opt_ CONST GUID *ClassGuid,
	_In_opt_ PCWSTR Enumerator,
	_In_opt_ HWND hwndParent,
	_In_ DWORD Flags)
{
	auto h = SetupDiGetClassDevsW(ClassGuid, Enumerator, hwndParent, Flags);
	return hdevinfo(h);
}

} // namespace usbip
