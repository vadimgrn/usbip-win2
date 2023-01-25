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

/*
 * @param idx zero-based index of usb device
 * @param dev usb device
 */
using usbip_usb_device_f = std::function<void(int idx, const usbip_usb_device &dev)>;

/*
 * @param dev_idx zero-based index of usb device
 * @param dev usb device
 * @param idx zero-based index of usb interface that belong to this usb device
 * @param intf usb interface
 */
using usbip_usb_interface_f = std::function<void(int dev_idx, const usbip_usb_device &dev, int idx, const usbip_usb_interface &intf)>;

/*
 * @param count number of usb devices
 */
using usbip_usb_device_cnt_f = std::function<void(int count)>;

bool enum_exportable_devices(
        SOCKET s, 
        const usbip_usb_device_f &on_dev, 
        const usbip_usb_interface_f &on_intf,
        const usbip_usb_device_cnt_f& on_dev_cnt = nullptr);

} // namespace usbip

