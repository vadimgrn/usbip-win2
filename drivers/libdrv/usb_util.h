/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbspec.h>
#include <usbip\proto.h>

inline auto& get_setup(usbip::header_cmd_submit &hdr)
{
	static_assert(sizeof(USB_DEFAULT_PIPE_SETUP_PACKET) == sizeof(hdr.setup));
	return *reinterpret_cast<USB_DEFAULT_PIPE_SETUP_PACKET*>(hdr.setup);
}

inline auto& get_submit_setup(usbip::header &hdr)
{
	return get_setup(hdr.cmd_submit);
}
