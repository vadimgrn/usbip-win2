/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"

#include <libusbip\vhci.h>
#include <spdlog\spdlog.h>

#include <print>

bool usbip::cmd_detach(void *p)
{
	auto &args = *reinterpret_cast<detach_args*>(p);

	auto dev = vhci::open();
	if (!dev) {
		spdlog::error(GetLastErrorMsg());
		return false;
	}

	auto ok = vhci::detach(dev.get(), args.port);

	if (!ok) {
		spdlog::error(GetLastErrorMsg());		
	} else if (args.port <= 0) {
		std::println("all ports are detached");
	} else {
                std::println("port {} is succesfully detached", args.port);
	}

	return ok;
}
