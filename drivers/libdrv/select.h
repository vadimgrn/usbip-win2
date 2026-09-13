/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>

struct _URB_SELECT_CONFIGURATION;
struct _URB_SELECT_INTERFACE;
struct _USBD_INTERFACE_INFORMATION;

namespace libdrv
{

enum { 
	SELECT_CONFIGURATION_STR_BUFSZ = 1024, 
	SELECT_INTERFACE_STR_BUFSZ = SELECT_CONFIGURATION_STR_BUFSZ 
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool is_valid(_In_ const _USBD_INTERFACE_INFORMATION *iface, _In_opt_ const void *cfg_end);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *select_configuration_str(
	_Out_writes_bytes_(len) char *buf, _In_ size_t len, _In_ const _URB_SELECT_CONFIGURATION *cfg);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
const char *select_interface_str(
	_Out_writes_bytes_(len) char *buf, _In_ size_t len, _In_ const _URB_SELECT_INTERFACE &iface);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_URB_SELECT_CONFIGURATION *clone(
	_Out_ ULONG &size, _In_ const _URB_SELECT_CONFIGURATION &src, _In_ POOL_TYPE pool_type, _In_ ULONG pooltag);

} // namespace libdrv
