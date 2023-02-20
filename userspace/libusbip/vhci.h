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

struct attach_info
{
        std::string hostname;
        std::string service; // TCP/IP port number or symbolic name
        std::string busid;
};

struct imported_device : attach_info
{
        int port; // [1..TOTAL_PORTS], hub port number

        UINT32 devid;
        usb_device_speed speed;

        UINT16 vendor;
        UINT16 product;
};

} // namespace usbip


namespace usbip::vhci
{

/*
 * @return full path to the driver device or empty string if the driver is not loaded 
 */
std::wstring get_path();

/**
 * @param path full path to the driver device
 * @return call GetLastError if returned handle is invalid
 */
Handle open(_In_ const std::wstring &path = get_path());

/*
 * @param dev handle of the driver device
 * @param success result of the operation
 * @return call GetLastError if success is false
 */
std::vector<imported_device> get_imported_devices(_In_ HANDLE dev, _Out_ bool &success);

/**
 * @param dev handle of the driver device
 * @param info arguments of the command
 * @return hub port number, [1..TOTAL_PORTS]. Call GetLastError() if zero is returned. 
 */
int attach(_In_ HANDLE dev, _In_ const attach_info &info);

/**
 * @param dev handle of the driver device
 * @param port hub port number
 * @return call GetLastError() if false is returned
 */
bool detach(_In_ HANDLE dev, _In_ int port);

} // namespace usbip::vhci
