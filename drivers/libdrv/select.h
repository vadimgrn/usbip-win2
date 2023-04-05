/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>

struct _URB_SELECT_CONFIGURATION;
struct _URB_SELECT_INTERFACE;

namespace libdrv
{

enum { 
	SELECT_CONFIGURATION_STR_BUFSZ = 1024, 
	SELECT_INTERFACE_STR_BUFSZ = SELECT_CONFIGURATION_STR_BUFSZ 
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *select_configuration_str(char *buf, size_t len, const _URB_SELECT_CONFIGURATION *cfg);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *select_interface_str(char *buf, size_t len, const _URB_SELECT_INTERFACE &iface);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_URB_SELECT_CONFIGURATION *clone(
	_Out_ ULONG &size, _In_ const _URB_SELECT_CONFIGURATION &src, _In_ POOL_FLAGS flags, _In_ ULONG pooltag);

} // namespace libdrv
