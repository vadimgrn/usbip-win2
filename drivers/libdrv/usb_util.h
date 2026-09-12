/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <kernelspecs.h>
#include <usbspec.h>
#include <usbip/proto.h>

namespace libdrv
{

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto& get_setup(_Inout_ usbip::header_cmd_submit &hdr)
{
	static_assert(sizeof(USB_DEFAULT_PIPE_SETUP_PACKET) == sizeof(hdr.setup));
	return *reinterpret_cast<USB_DEFAULT_PIPE_SETUP_PACKET*>(hdr.setup);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline const auto& get_setup(_In_ const usbip::header_cmd_submit &hdr)
{
	static_assert(sizeof(USB_DEFAULT_PIPE_SETUP_PACKET) == sizeof(hdr.setup));
	return *reinterpret_cast<const USB_DEFAULT_PIPE_SETUP_PACKET*>(hdr.setup);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto& get_submit_setup(_Inout_ usbip::header &hdr)
{
	return get_setup(hdr.cmd_submit);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline const auto& get_submit_setup(_In_ const usbip::header &hdr)
{
	return get_setup(hdr.cmd_submit);
}

} // namespace libdrv
