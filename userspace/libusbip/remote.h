/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn
 */

#pragma once

#include "dllspec.h"
#include "win_handle.h"
#include "win_socket.h"

#include <usbspec.h>

#include <string>
#include <vector>
#include <optional>

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


#if __cplusplus >= 202002L
/**
 * The call is blocking.
 * @param hostname name or IP address of a host to connect to
 * @param service TCP/IP port number or symbolic name
 * @param stoken stop token to cancel the connection attempt
 * @return call GetLastError() if returned handle is invalid
 */
inline Socket connect(
        _In_ const char *hostname,
        _In_ const char *service,
        _In_ std::stop_token stoken)
{
        Socket sock;
        if (stoken.stop_requested()) {
                SetLastError(ERROR_CANCELLED);
                return sock;
        }

        NullableHandle cancel_evt;
        auto on_stop = [&cancel_evt] { SetEvent(cancel_evt.get()); };

        using stop_cb_t = std::stop_callback<decltype(on_stop)>;
        std::optional<stop_cb_t> stop_cb;

        if (stoken.stop_possible()) {
                cancel_evt.reset(CreateEvent(nullptr, true, false, nullptr));
                if (!cancel_evt) {
                        return sock;
                }
                stop_cb.emplace(stoken, on_stop);
        }

        sock = connect(hostname, service, cancel_evt.get());
        return sock;
}
#endif // __cplusplus

/**
 * @param s socket handle
 * @return devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<usb_device>> get_exportable_devices(_In_ SOCKET s);

} // namespace usbip
