/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn
 */

#pragma once

#include "dllspec.h"
#include "win_handle.h"
#include "win_socket.h"

#include <usbspec.h>

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <functional>

#include <stop_token>

namespace usbip
{

struct usb_interface 
{
        UINT8 bInterfaceClass{};
        UINT8 bInterfaceSubClass{};
        UINT8 bInterfaceProtocol{};
};

struct usb_device 
{
        std::string path;
        std::string busid;

        UINT32 busnum{};
        UINT32 devnum{};
        USB_DEVICE_SPEED speed = UsbLowSpeed;

        UINT16 idVendor{};
        UINT16 idProduct{};
        UINT16 bcdDevice{}; // Device Release Number

        UINT8 bDeviceClass{};
        UINT8 bDeviceSubClass{};
        UINT8 bDeviceProtocol{};

        UINT8 bConfigurationValue{};
        UINT8 bNumConfigurations{};

        std::vector<usb_interface> interfaces;
};

/**
 * @return default TCP/IP port number of usbip server
 */
USBIP_API const char *get_tcp_port() noexcept;

/**
 * The call is blocking.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param cancel_event optional event handle to cancel the connection attempt
 * @return call GetLastError() if returned handle is invalid
 */
USBIP_API Socket connect(
        _In_ const char *hostname,
        _In_ const char *service,
        _In_opt_ HANDLE cancel_event = HANDLE{});


/**
 * The call is blocking.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param stoken stop token to cancel the connection attempt
 * @return call GetLastError() if returned handle is invalid
 */
USBIP_API Socket connect(
        _In_ const char *hostname,
        _In_ const char *service,
        _In_ std::stop_token stoken);

using connect_callback = std::function<void(Socket sock, DWORD error)>;

/**
 * Connects to a remote host asynchronously on the Windows Threadpool.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param callback invoked with connected socket or error code on completion
 * @param cancel_event optional event handle to cancel the connection attempt
 * @return true if successfully queued to threadpool, false otherwise
 */
USBIP_API bool connect_async(
        _In_ const char *hostname,
        _In_ const char *service,
        _In_ connect_callback callback,
        _In_opt_ HANDLE cancel_event = HANDLE{});

/**
 * Connects to a remote host asynchronously on the Windows Threadpool.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param callback invoked with connected socket or error code on completion
 * @param stoken stop token to cancel the connection attempt
 * @return true if successfully queued to threadpool, false otherwise
 */
USBIP_API bool connect_async(
        _In_ const char *hostname,
        _In_ const char *service,
        _In_ connect_callback callback,
        _In_ std::stop_token stoken);

/**
 * Retrieves exportable devices from an established connection.
 * The call is blocking.
 * @param s socket handle
 * @param cancel_event optional event handle to cancel the operation
 * @return devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<usb_device>> get_exportable_devices(
        _In_ SOCKET s,
        _In_opt_ HANDLE cancel_event = HANDLE{});

/**
 * Retrieves exportable devices from an established connection.
 * The call is blocking.
 * @param s socket handle
 * @param stoken stop token to cancel the operation
 * @return devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<usb_device>> get_exportable_devices(
        _In_ SOCKET s,
        _In_ std::stop_token stoken);

/**
 * Connects to the remote host, retrieves exportable devices, and closes the connection.
 * The call is blocking.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param cancel_event optional event handle to cancel the operation
 * @return devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<usb_device>> query_exportable_devices(
        _In_ const char *hostname,
        _In_ const char *service,
        _In_opt_ HANDLE cancel_event = HANDLE{});

/**
 * Connects to the remote host, retrieves exportable devices, and closes the connection.
 * The call is blocking.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param stoken stop token to cancel the operation
 * @return devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<usb_device>> query_exportable_devices(
        _In_ const char *hostname,
        _In_ const char *service,
        _In_ std::stop_token stoken);

using query_devices_callback = std::function<void(
        std::optional<std::vector<usb_device>> devices,
        DWORD error)>;

/**
 * Queries exportable devices asynchronously on the Windows Threadpool.
 * Handles DNS resolution, connection, protocol query, and socket teardown.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param callback invoked with query results on completion
 * @param cancel_event optional event handle to cancel the operation
 * @return true if successfully queued to threadpool, false otherwise
 */
USBIP_API bool query_exportable_devices_async(
        _In_ const char *hostname,
        _In_ const char *service,
        _In_ query_devices_callback callback,
        _In_opt_ HANDLE cancel_event = HANDLE{});

/**
 * Queries exportable devices asynchronously on the Windows Threadpool.
 * Handles DNS resolution, connection, protocol query, and socket teardown.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param callback invoked with query results on completion
 * @param stoken stop token to cancel the operation
 * @return true if successfully queued to threadpool, false otherwise
 */
USBIP_API bool query_exportable_devices_async(
        _In_ const char *hostname,
        _In_ const char *service,
        _In_ query_devices_callback callback,
        _In_ std::stop_token stoken);

} // namespace usbip
