/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "win_handle.h"
#include <usbspec.h>

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
        int port; // [1..get_total_ports()], hub port number

        UINT32 devid;
        USB_DEVICE_SPEED speed;

        UINT16 vendor;
        UINT16 product;
};

} // namespace usbip


namespace usbip::vhci
{

/**
 * @return total number of root hub ports
 */
int get_total_ports() noexcept;

/**
 * Open driver's device interface
 * @return handle, call GetLastError() if it is invalid
 */
Handle open();

/*
 * @param dev handle of the driver device
 * @param success result of the operation
 * @return call GetLastError if success is false
 */
std::vector<imported_device> get_imported_devices(_In_ HANDLE dev, _Out_ bool &success);

/**
 * @param dev handle of the driver device
 * @param info arguments of the command
 * @return hub port number, [1..get_total_ports()]. Call GetLastError() if zero is returned. 
 */
int attach(_In_ HANDLE dev, _In_ const attach_info &info);

/**
 * @param dev handle of the driver device
 * @param port hub port number, zero or -1 mean detach all ports
 * @return call GetLastError() if false is returned
 */
bool detach(_In_ HANDLE dev, _In_ int port);

} // namespace usbip::vhci
