/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbip.h"
#include "strings.h"

#include <usbip\proto_op.h>
#include <libusbip\remote.h>

#include <spdlog\spdlog.h>

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

void on_device(int, const usbip_usb_device &d)
{
	auto &ids = get_ids();
	auto prod = get_product(ids, d.idVendor, d.idProduct);
	auto csp = get_class(ids, d.bDeviceClass, d.bDeviceSubClass, d.bDeviceProtocol);

	printf( "%11s: %s\n"
		"%11s: %s\n"
		"%11s: %s\n",
		d.busid, prod.c_str(),
		"", d.path,
		"", csp.c_str());

	if (!d.bNumInterfaces) {
		printf("\n");
	}
}

void on_interface(int, const usbip_usb_device &d, int idx, const usbip_usb_interface &r)
{
	auto &ids = get_ids();

	auto csp = get_class(ids, r.bInterfaceClass, r.bInterfaceSubClass, r.bInterfaceProtocol);
	printf("%11s: %2d - %s\n", "", idx, csp.c_str());

	if (idx + 1 == d.bNumInterfaces) { // last
		printf("\n");
	}
}

} // namespace


bool usbip::cmd_list(void *p)
{
	auto &r = *reinterpret_cast<list_args*>(p);

	auto sock = connect(r.remote.c_str(), global_args.tcp_port.c_str());
	if (!sock) {
		spdlog::error(GetLastErrorMsg());
		return false;
	}

	spdlog::debug("connected to {}:{}", r.remote, global_args.tcp_port);

	if (!enum_exportable_devices(sock.get(), on_device, on_interface, on_device_count)) {
		spdlog::error(GetLastErrorMsg());
		return false;
	}

	return true;
}
