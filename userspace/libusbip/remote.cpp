/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn
 */

#include "remote.h"
#include "network.h"
#include "dbgcode.h"

#include <usbip\proto_op.h>
#include <spdlog\spdlog.h>

bool usbip::enum_exportable_devices(
	SOCKET s, 
	const usbip_usb_device_f &on_dev, 
	const usbip_usb_interface_f &on_intf,
	const usbip_usb_device_cnt_f &on_dev_cnt)
{
	assert(s != INVALID_SOCKET);
	
	if (!net::send_op_common(s, OP_REQ_DEVLIST)) {
		spdlog::error("send_op_common");
		return false;
	}

	if (auto err = net::recv_op_common(s, OP_REP_DEVLIST)) {
		spdlog::error("recv_op_common {}", errt_str(err));
		return false;
	}

	op_devlist_reply reply;
	
	if (net::recv(s, &reply, sizeof(reply))) {
		PACK_OP_DEVLIST_REPLY(false, &reply);
	} else {
		spdlog::error("recv op_devlist_reply");
		return false;
	}

	spdlog::debug("{} exportable device(s)", reply.ndev);
	assert(reply.ndev <= INT_MAX);

	if (on_dev_cnt) {
		on_dev_cnt(reply.ndev);
	}

	for (UINT32 i = 0; i < reply.ndev; ++i) {

		usbip_usb_device dev;

		if (net::recv(s, &dev, sizeof(dev))) {
			usbip_net_pack_usb_device(false, &dev);
			on_dev(i, dev);
		} else {
			spdlog::error("recv usbip_usb_device[{}]", i);
			return false;
		}

		for (int j = 0; j < dev.bNumInterfaces; ++j) {

			usbip_usb_interface intf;

			if (net::recv(s, &intf, sizeof(intf))) {
				usbip_net_pack_usb_interface(false, &intf);
				on_intf(i, dev, j, intf);
			} else {
				spdlog::error("recv usbip_usb_intf[{}]", j);
				return false;
			}
		}
	}

	return true;
}
