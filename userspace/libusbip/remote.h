/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "win_handle.h"
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
 * Creates a manual-reset event for cancelling operations.
 * The event can be passed as cancel_event to connect() or isolate_port_volumes().
 * @param initial_state whether the event is initially signaled
 * @return event handle, or an invalid NullableHandle on failure (call GetLastError())
 * @see connect
 * @see isolate_port_volumes
 */
USBIP_API NullableHandle create_cancel_event(_In_ bool initial_state = false) noexcept;

/**
 * The call is blocking.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param cancel_event optional manual-reset event handle to cancel the connection attempt
 *        (use create_cancel_event() to create it)
 * @return call GetLastError() if returned handle is invalid
 * @see create_cancel_event
 */
USBIP_API Socket connect(
        _In_ std::string_view hostname,
        _In_ std::string_view service,
        _In_opt_ HANDLE cancel_event = HANDLE{});

/**
 * Cancel pending synchronous or asynchronous I/O operations on the socket by shutting it down.
 * The socket cannot be used for further I/O and must be closed after this call.
 * @param s socket handle
 * @return call GetLastError() if false is returned
 */
USBIP_API bool cancel_io(_In_ SOCKET s) noexcept;

/**
 * Retrieves exportable devices from an established connection.
 * The call is blocking.
 * @param s socket handle
 * @return devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<usb_device>> get_exportable_devices(_In_ SOCKET s);


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

#endif // __cplusplus >= 202002L

} // namespace usbip
