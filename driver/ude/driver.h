/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <ntdef.h>

namespace usbip
{

const ULONG USBIP_VHCI_POOL_TAG = 'ICHV';

inline auto ptr4log(const void *ptr) // use format "%04x"
{
	auto n = reinterpret_cast<uintptr_t>(ptr);
	return static_cast<UINT32>(n);
}

} // namespace usbip
