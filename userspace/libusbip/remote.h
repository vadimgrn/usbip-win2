/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn
 */

#pragma once

#include "win_socket.h"

#include <usbspec.h>

#include <string>
#include <string_view>
#include <optional>
#include <vector>

#if __cplusplus >= 202002L
  #include <stop_token>
#endif

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
        USB_DEVICE_SPEED speed{}; // UsbLowSpeed

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
 * @param cancel_event optional manual-reset event handle to cancel the connection attempt
 * @return call GetLastError() if returned handle is invalid
 */
USBIP_API Socket connect(
        _In_ std::string_view hostname,
        _In_ std::string_view service,
        _In_opt_ HANDLE cancel_event = HANDLE{});


/**
 * Completion callback invoked on a Windows Threadpool worker thread.
 *
 * @param sock connected socket on success, or invalid socket on failure
 * @param error ERROR_SUCCESS on success, or Win32/Winsock error code on failure
 */
using connect_callback = std::function<void(Socket sock, DWORD error)>;

/**
 * Connects to a remote host asynchronously on the Windows Threadpool.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param callback invoked with connected socket or error code on completion
 * @param cancel_event optional manual-reset event handle to cancel the connection attempt;
 *        must remain valid until the callback is invoked
 * @return true if successfully queued to threadpool, call GetLastError() if false is returned
 */
USBIP_API bool connect_async(
        _In_ std::string_view hostname,
        _In_ std::string_view service,
        _In_ connect_callback callback,
        _In_opt_ HANDLE cancel_event = HANDLE{});

/**
 * Retrieves exportable devices from an established connection.
 * The call is blocking.
 * @param s socket handle
 * @param cancel_event optional manual-reset event handle to cancel the operation;
 *        cancellation shuts down the socket, it can't be used for further I/O
 * @return devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<usb_device>> get_exportable_devices(
        _In_ SOCKET s, _In_opt_ HANDLE cancel_event = HANDLE{});

/**
 * Connects to the remote host, retrieves exportable devices, and closes the connection.
 * The call is blocking.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param cancel_event optional manual-reset event handle to cancel the operation
 * @return devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<usb_device>> get_exportable_devices(
        _In_ std::string_view hostname,
        _In_ std::string_view service,
        _In_opt_ HANDLE cancel_event = HANDLE{});

/**
 * Completion callback invoked on a Windows Threadpool worker thread.
 *
 * @param devices list of exportable devices on success, or std::nullopt on failure
 * @param error ERROR_SUCCESS on success, or Win32/Winsock error code on failure
 */
using get_exportable_devices_callback = std::function<
        void(std::optional<std::vector<usb_device>> devices, DWORD error)>;

/**
 * Retrieves exportable devices asynchronously on the Windows Threadpool.
 * Handles DNS resolution, connection, protocol query, and socket teardown.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param callback invoked with query results on completion
 * @param cancel_event optional manual-reset event handle to cancel the operation;
 *        must remain valid until the callback is invoked
 * @return true if successfully queued to threadpool, call GetLastError() if false is returned
 */
USBIP_API bool get_exportable_devices_async(
        _In_ std::string_view hostname,
        _In_ std::string_view service,
        _In_ get_exportable_devices_callback callback,
        _In_opt_ HANDLE cancel_event = HANDLE{});


#if __cplusplus >= 202002L

/**
 * The call is blocking.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param stoken stop token to cancel the connection attempt
 * @return call GetLastError() if returned handle is invalid
 */
USBIP_API Socket connect(
        _In_ std::string_view hostname,
        _In_ std::string_view service,
        _In_ std::stop_token stoken);

/**
 * Connects to a remote host asynchronously on the Windows Threadpool.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param callback invoked with connected socket or error code on completion
 * @param stoken stop token to cancel the connection attempt
 * @return true if successfully queued to threadpool, call GetLastError() if false is returned
 */
USBIP_API bool connect_async(
        _In_ std::string_view hostname,
        _In_ std::string_view service,
        _In_ connect_callback callback,
        _In_ std::stop_token stoken);

/**
 * Retrieves exportable devices from an established connection.
 * The call is blocking.
 * @param s socket handle
 * @param stoken stop token to cancel the operation
 * @return devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<usb_device>> get_exportable_devices(
        _In_ SOCKET s, _In_ std::stop_token stoken);

/**
 * Connects to the remote host, retrieves exportable devices, and closes the connection.
 * The call is blocking.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param stoken stop token to cancel the operation
 * @return devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<usb_device>> get_exportable_devices(
        _In_ std::string_view hostname,
        _In_ std::string_view service,
        _In_ std::stop_token stoken);

/**
 * Retrieves exportable devices asynchronously on the Windows Threadpool.
 * Handles DNS resolution, connection, protocol query, and socket teardown.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param callback invoked with query results on completion
 * @param stoken stop token to cancel the operation
 * @return true if successfully queued to threadpool, call GetLastError() if false is returned
 */
USBIP_API bool get_exportable_devices_async(
        _In_ std::string_view hostname,
        _In_ std::string_view service,
        _In_ get_exportable_devices_callback callback,
        _In_ std::stop_token stoken);

#endif // __cplusplus >= 202002L

} // namespace usbip
