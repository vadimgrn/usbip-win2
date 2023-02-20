/*
 * Copyright (C) 2021, 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbspec.h>
#include <usbip\proto.h>

inline auto& get_setup(usbip_header_cmd_submit &hdr)
{
	static_assert(sizeof(USB_DEFAULT_PIPE_SETUP_PACKET) == sizeof(hdr.setup));
	return *reinterpret_cast<USB_DEFAULT_PIPE_SETUP_PACKET*>(hdr.setup);
}

inline auto& get_submit_setup(usbip_header &hdr)
{
	return get_setup(hdr.u.cmd_submit);
}
