/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "strings.h"

#include <libusbip\vhci.h>
#include <libusbip\persistent.h>

#include <spdlog\spdlog.h>
#include <print>

namespace
{

using namespace usbip;

void on_device_count(int count)
{
	if (count) {
		std::println("Exportable USB devices\n"
			     "======================");
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

        std::print("{}", lines);
}

void on_interface(int, const usb_device &d, int idx, const usb_interface &r)
{
	auto &ids = get_ids();
	auto csp = get_class(ids, r.bInterfaceClass, r.bInterfaceSubClass, r.bInterfaceProtocol);

	auto s = std::format("{:11}: {:2} - {}\n", "", idx, csp);
	if (idx + 1 == d.bNumInterfaces) { // last
		s += '\n';
	}

        std::print("{}", s);
}

auto list_persistent_devices()
{
	if (auto dev = vhci::open(); !dev) {
		spdlog::error(GetLastErrorMsg());
                return false;
        } else if (auto v = vhci::get_persistent(dev.get()); !v) {
		spdlog::error(GetLastErrorMsg());
                return false;
        } else for (auto &i: *v) {
                auto &loc = i.location;
                std::println("{}:{}/{}\t{}", loc.hostname, loc.service, loc.busid, i.serial);
        }

        return true;
}

} // namespace


bool usbip::cmd_list(void *p)
{
	auto &args = *reinterpret_cast<list_args*>(p);
	if (args.persistent) {
		return list_persistent_devices();
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
