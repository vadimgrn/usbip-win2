/*
 * Copyright (c) 2021-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "strings.h"

#include <libusbip\vhci.h>
#include <libusbip\persistent.h>

#pragma warning(push)
#pragma warning(disable: 4389) // signed/unsigned mismatch in spdlog headers
#include <spdlog\spdlog.h>
#pragma warning(pop)

namespace
{

using namespace usbip;

void on_device_count(int count)
{
	if (count) {
		printf("Exportable USB devices\n"
			"======================\n");
	}
}

void on_device(int, const usb_device &d)
{
	auto &ids = get_ids();
	auto prod = get_product(ids, d.idVendor, d.idProduct);
	auto csp = get_class(ids, d.bDeviceClass, d.bDeviceSubClass, d.bDeviceProtocol);

	auto lines = std::format(
			"{:^11}: {}\n"
			"{:11}: {}\n"
			"{:11}: {}\n",
			d.busid, prod,
			"", d.path,
			"", csp);

	if (!d.bNumInterfaces) {
		lines += '\n';
	}

	printf(lines.c_str());
}

void on_interface(int, const usb_device &d, int idx, const usb_interface &r)
{
	auto &ids = get_ids();
	auto csp = get_class(ids, r.bInterfaceClass, r.bInterfaceSubClass, r.bInterfaceProtocol);

	auto s = std::format("{:11}: {:2} - {}\n", "", idx, csp);
	if (idx + 1 == d.bNumInterfaces) { // last
		s += '\n';
	}

	printf(s.c_str());
}

auto list_stashed_devices()
{
	bool success{};
	
	if (auto dev = vhci::open(); !dev) {
		spdlog::error(GetLastErrorMsg());
	} else if (auto v = vhci::get_persistent(dev.get(), success); !success) {
		spdlog::error(GetLastErrorMsg());
	} else for (auto &i: v) {
		printf("%s:%s/%s\n", i.hostname.c_str(), i.service.c_str(), i.busid.c_str());
	}

	return success;
}

} // namespace


bool usbip::cmd_list(void *p)
{
	auto &args = *reinterpret_cast<list_args*>(p);
	if (args.stashed) {
		return list_stashed_devices();
	}

	auto sock = connect(args.remote.c_str(), global_args.tcp_port.c_str());
	if (!sock) {
		spdlog::error(GetLastErrorMsg());
		return false;
	}

	spdlog::debug("connected to {}:{}", args.remote, global_args.tcp_port);

	if (!enum_exportable_devices(sock.get(), on_device, on_interface, on_device_count)) {
		spdlog::error(GetLastErrorMsg());
		return false;
	}

	return true;
}
