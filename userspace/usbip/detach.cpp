/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "log.h"
#include <libusbip/vhci.h>

#include <print>

bool usbip::cmd_detach(const detach_args &args)
{
	auto dev = vhci::open();
	if (!dev) {
		log::error(get_last_error_msg());
		return false;
	}

	auto ok = vhci::detach(dev.get(), args.port);

	if (!ok) {
		log::error(get_last_error_msg());		
	} else if (args.port <= 0) {
		std::println("all ports are detached");
	} else {
                std::println("port {} is successfully detached", args.port);
	}

	return ok;
}
