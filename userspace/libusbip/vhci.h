/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "win_handle.h"
#include <usbip/ch9.h>

#include <string>
#include <vector>

/*
 * Strings encoding is UTF8. 
 */

namespace usbip
{

struct imported_device
{
        int port; // [1..TOTAL_PORTS], hub port number

        std::string hostname;
        std::string service; // TCP/IP port number or symbolic name
        std::string busid;

        UINT32 devid;
        usb_device_speed speed;

        UINT16 vendor;
        UINT16 product;
};

struct attach_info
{
        std::string hostname;
        std::string service;
        std::string busid;
};

} // namespace usbip


namespace usbip::vhci
{

std::wstring get_path();
Handle open(_In_ const std::wstring &path = get_path());

std::vector<imported_device> get_imported_devices(_In_ HANDLE dev, _Out_ bool &success);

int attach(_In_ HANDLE dev, _In_ const attach_info &info);
bool detach(_In_ HANDLE dev, _In_ int port);

} // namespace usbip::vhci
