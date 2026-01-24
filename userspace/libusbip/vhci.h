/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "dllspec.h"
#include "win_handle.h"
#include <usbspec.h>

#include <string>
#include <vector>

/*
 * Strings encoding is UTF8. 
 */

namespace usbip
{

struct device_location
{
        std::string hostname;
        std::string service; // TCP/IP port number or symbolic name
        std::string busid;
};

struct imported_device
{
        device_location location;
        int port{}; // hub port number, >= 1

        UINT32 devid{};
        USB_DEVICE_SPEED speed = UsbLowSpeed;

        UINT16 vendor{};
        UINT16 product{};
};

enum class state { unplugged, connecting, connected, plugged, disconnected, unplugging };

struct device_state
{
        imported_device device;
        state state = state::unplugged;
        ULONG source_id; // unique for current set of event issuers
};

} // namespace usbip


namespace usbip::vhci
{

/**
 * Open driver's device interface
 * @param overlapped open the device for asynchronous I/O
 * @return handle, call GetLastError() if it is invalid
 */
USBIP_API Handle open(_In_ bool overlapped = false);

/**
 * @param dev handle of the driver device
 * @param success call GetLastError() if false is returned
 * @return imported devices
 */
USBIP_API std::vector<imported_device> get_imported_devices(_In_ HANDLE dev, _Out_ bool &success);

/**
 * @param dev handle of the driver device
 * @param location remote device to attach to
 * @return hub port number, >= 1. Call GetLastError() if zero is returned. 
 */
USBIP_API int attach(_In_ HANDLE dev, _In_ const device_location &location);

/**
 * @param dev handle of the driver device
 * @param location stop attach attempts to this device or stop all active attach attempts if NULL
 * @return number of canceled requests, call GetLastError() if -1 is returned
 */
USBIP_API int stop_attach_attempts(_In_ HANDLE dev, _In_opt_ const device_location *location);

/**
 * @param dev handle of the driver device
 * @param port hub port number, <= 0 means detach all ports
 * @return call GetLastError() if false is returned
 */
USBIP_API bool detach(_In_ HANDLE dev, _In_ int port);

/**
 * @return textual representation of the given constant
 */
USBIP_API const char* get_state_str(_In_ state state) noexcept;

/**
 * Read this number of bytes and pass them to get_device_state()
 * @return bytes to read from the device handle, constant
 */
USBIP_API DWORD get_device_state_size() noexcept;

/**
 * @param result constructed from passed data
 * @param data that was read from the device handle
 * @param length data length, must be equal to get_device_state_size()
 * @return call GetLastError() if false is returned
 */
USBIP_API bool get_device_state(_Inout_ device_state &result, _In_ const void *data, _In_ DWORD length);

/**
 * @param dev handle of the driver device that must be opened for serialized I/O
 * @param result data that was obtained by read operation on the given handle
 * @return call GetLastError() if false is returned
 */
USBIP_API bool read_device_state(_In_ HANDLE dev, _Inout_ device_state &result);

} // namespace usbip::vhci
