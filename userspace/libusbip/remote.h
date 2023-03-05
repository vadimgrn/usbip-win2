/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn
 */

#pragma once

#include "dllspec.h"
#include "win_socket.h"

#include <usbspec.h>
#include <string>

namespace usbip
{

struct usb_device 
{
        std::string path;
        std::string busid;

        UINT32 busnum;
        UINT32 devnum;
        USB_DEVICE_SPEED speed;

        UINT16 idVendor;
        UINT16 idProduct;
        UINT16 bcdDevice; // Device Release Number

        UINT8 bDeviceClass;
        UINT8 bDeviceSubClass;
        UINT8 bDeviceProtocol;

        UINT8 bConfigurationValue;

        UINT8 bNumConfigurations;
        UINT8 bNumInterfaces;
};

struct usb_interface 
{
        UINT8 bInterfaceClass;
        UINT8 bInterfaceSubClass;
        UINT8 bInterfaceProtocol;
        UINT8 padding;	/* alignment */
};

/**
 * @return default TCP/IP port number of usbip server
 */
USBIP_API const char *get_tcp_port() noexcept;

/**
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number of symbolic name
 * @return call GetLastError() if returned handle is invalid
 */
USBIP_API Socket connect(_In_ const char *hostname, _In_ const char *service);

/**
 * @param idx zero-based index of usb device
 * @param dev usb device
 */
using usb_device_f = std::function<void(_In_ int idx, _In_ const usb_device &dev)>;

/**
 * @param dev_idx zero-based index of usb device
 * @param dev usb device
 * @param idx zero-based index of usb interface that belong to this usb device
 * @param intf usb interface
 */
using usb_interface_f = std::function<void(_In_ int dev_idx, _In_ const usb_device &dev, int idx, const usb_interface &intf)>;

/**
 * @param count number of usb devices
 */
using usb_device_cnt_f = std::function<void(_In_ int count)>;

/**
 * @param s socket handle
 * @param on_dev will be called for every usb device
 * @param on_intf will be called for every usb interface of usb device
 * @param on_dev_cnt will be called once before other callbacks
 * @return call GetLastError() if false is returned
 */
USBIP_API bool enum_exportable_devices(
        _In_ SOCKET s, 
        _In_ const usb_device_f &on_dev, 
        _In_ const usb_interface_f &on_intf,
        _In_opt_ const usb_device_cnt_f &on_dev_cnt = nullptr);

} // namespace usbip
