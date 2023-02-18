/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn
 */

#pragma once

#include "win_socket.h"

struct usbip_usb_device;
struct usbip_usb_interface;

namespace usbip
{

Socket connect(_In_ const char *hostname, _In_ const char *service);

/*
 * @param idx zero-based index of usb device
 * @param dev usb device
 */
using usbip_usb_device_f = std::function<void(_In_ int idx, _In_ const usbip_usb_device &dev)>;

/*
 * @param dev_idx zero-based index of usb device
 * @param dev usb device
 * @param idx zero-based index of usb interface that belong to this usb device
 * @param intf usb interface
 */
using usbip_usb_interface_f = std::function<void(_In_ int dev_idx, _In_ const usbip_usb_device &dev, int idx, const usbip_usb_interface &intf)>;

/*
 * @param count number of usb devices
 */
using usbip_usb_device_cnt_f = std::function<void(_In_ int count)>;

bool enum_exportable_devices(
        _In_ SOCKET s, 
        _In_ const usbip_usb_device_f &on_dev, 
        _In_ const usbip_usb_interface_f &on_intf,
        _In_opt_ const usbip_usb_device_cnt_f& on_dev_cnt = nullptr);

} // namespace usbip

