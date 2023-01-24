/*
 * Copyright (C) 2023 Vadym Hrynchyshyn
 */

#pragma once

#include <WinSock2.h>
#include <functional>

struct usbip_usb_device;
struct usbip_usb_interface;

namespace usbip
{

using usbip_usb_device_f = std::function<void(int, const usbip_usb_device&)>;
using usbip_usb_interface_f = std::function<void(int, const usbip_usb_device&, int, const usbip_usb_interface&)>;

bool enum_exportable_devices(SOCKET s, const usbip_usb_device_f &on_dev, const usbip_usb_interface_f &on_intf);

} // namespace usbip

