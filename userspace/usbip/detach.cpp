/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"

#include <libusbip\vhci.h>
#include <spdlog\spdlog.h>

bool usbip::cmd_detach(void *p)
{
	auto &r = *reinterpret_cast<detach_args*>(p);

	auto dev = vhci::open();
	if (!dev) {
		return false;
	}

	auto ok = vhci::detach(dev.get(), r.port);

	if (!ok) {
		spdlog::error("can't detach port {}", r.port);
	} else if (r.port <= 0) {
		printf("all ports are detached\n");
	} else {
		printf("port %d is succesfully detached\n", r.port);
	}

	return ok;
}
