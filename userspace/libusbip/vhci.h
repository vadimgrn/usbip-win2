/*
 * Copyright (c) 2021-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "dllspec.h"
#include "win_handle.h"
#include <usbspec.h>

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <memory>

#include <stop_token>

/*
 * Strings encoding is UTF8.
 */

namespace usbip::vhci
{

/*
 * A virtual device isolation can be session/user based.
 */ 
#ifndef USBIP_VHCI_ISOLATION_DEFINED
#define USBIP_VHCI_ISOLATION_DEFINED
  enum class isolation : unsigned char { none, session, user };
#endif

/**
 * How to optimize data reception over the network
 * - zero_copy - use a dedicated thread which executes a loop with two blocking calls
 *   - receive USBIP header
 *   - receive USBIP payload, if any
 *   - Data is written directly to URB.TransferBuffer, there is no additional copying
 *   - Should be used for storage devices, webcams etc.
 * - low_latency - use WSK Event Callback Functions
 *   - Receive thread is not used, but the data is first written
 *     to an intermediate ring buffer, then copied to URB.TransferBuffer
 *   - Should be used for devices that generate small amounts of data
 *     but at a high frequency, such as HID keyboard/mouse, etc.
 */
enum class receive_mode : unsigned char { zero_copy, low_latency };

} // namespace usbip::vhci


namespace usbip
{

using vhci::isolation;
using vhci::receive_mode;

struct device_location
{
        std::string hostname;
        std::string service; ///< TCP/IP port number or symbolic name
        std::string busid;
};

/**
 * Possible values for user (used only when iso_mode is isolation::user):
 * - Empty string (""): automatically resolves to the calling process's current User SID (TokenUser).
 * - Standard string SID: e.g. "S-1-5-21-XXXXXXXXXX-XXXXXXXXXX-XXXXXXXXXX-XXXX"
 *   (parsed via ConvertStringSidToSid).
 * - Windows account name: e.g. "DOMAIN\\Username" or "Username"
 *   (resolved via LookupAccountName).
 *
 * @see generate_device_serial
 * @see validate_device_serial
 * @see get_device_serial_maxlen
 */
struct device_config
{
        device_location location;
        std::string serial; ///< optional device serial number if you want to set/override it
        receive_mode recv_mode = receive_mode::zero_copy; ///< how to optimize data reception over the network
        isolation iso_mode = isolation::none; ///< device isolation mode
        std::string user; ///< optional user identity (account name or SID) for isolation::user
};


struct imported_device
{
        device_config config;
        int port{}; ///< hub port number, >= 1

        UINT32 devid{};
        USB_DEVICE_SPEED speed = UsbLowSpeed;

        UINT16 vendor{};
        UINT16 product{};
};

enum class state { unplugged, connecting, connected, plugged, disconnected, unplugging };

/**
 * There can be multiple event sources for the same device,
 * each of them emits events with a unique source_id.
 */
struct device_state
{
        imported_device device;
        state state = state::unplugged;
        ULONG source_id{};
};

/**
 * @return max length of usb device serial number, constant
 */
USBIP_API int get_device_serial_maxlen() noexcept;

/**
 * Serial number must contain no more than N alphanumeric ASCII characters.
 * @param serial number of usb device
 * @return call GetLastError() if false is returned
 * @see get_device_serial_maxlen
 */
USBIP_API bool validate_device_serial(_In_ const std::string &serial) noexcept;

/**
 * The function is thread-safe.
 * @return unique serial number for the device
 */
USBIP_API std::string generate_device_serial();

} // namespace usbip

#include "persistent.h"

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
 * @return imported devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<imported_device>> get_imported_devices(_In_ HANDLE dev);

/**
 * Convenience overload that opens a temporary driver handle to get imported devices.
 * @return imported devices if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<std::vector<imported_device>> get_imported_devices();

struct attach_args
{
        device_config config;
        bool once{}; ///< do not start automatic attach attempts if cannot connect
};

/**
 * @param dev handle of the driver device
 * @param args arguments
 * @return hub port number, >= 1. Call GetLastError() if zero is returned.
 */
USBIP_API int attach(_In_ HANDLE dev, _In_ const attach_args &args);

inline auto attach(_In_ HANDLE dev, _In_ const device_config &config)
{
        return attach(dev, attach_args{ config });
}

/**
 * Attaches a device using a temporary driver handle.
 * @param args arguments
 * @return hub port number, >= 1. Call GetLastError() if zero is returned.
 */
USBIP_API int attach(_In_ const attach_args &args);

inline auto attach(_In_ const device_config &config)
{
        return attach(attach_args{ config });
}

using attach_callback = std::function<void(int port, DWORD error)>;

/**
 * Asynchronously attach a remote USB device using the Windows Threadpool.
 * Returns immediately without blocking the caller.
 *
 * @param dev handle of the driver device
 * @param args arguments
 * @param callback completion callback invoked on threadpool worker thread
 * @param cancel_event optional event handle to cancel the attach operation
 * @return true if successfully queued to threadpool, call GetLastError() if false is returned
 */
USBIP_API bool attach_async(
        _In_ HANDLE dev,
        _In_ const attach_args &args,
        _In_ attach_callback callback,
        _In_opt_ HANDLE cancel_event = HANDLE{});

inline bool attach_async(
        _In_ HANDLE dev,
        _In_ const device_config &config,
        _In_ attach_callback callback,
        _In_opt_ HANDLE cancel_event = HANDLE{})
{
        return attach_async(dev, attach_args{ config }, std::move(callback), cancel_event);
}

USBIP_API bool attach_async(
        _In_ HANDLE dev,
        _In_ const attach_args &args,
        _In_ attach_callback callback,
        _In_ std::stop_token stoken);

inline bool attach_async(
        _In_ HANDLE dev,
        _In_ const device_config &config,
        _In_ attach_callback callback,
        _In_ std::stop_token stoken)
{
        return attach_async(dev, attach_args{ config }, std::move(callback), stoken);
}

/**
 * Asynchronously attach a remote USB device using the Windows Threadpool.
 * Uses an independent driver handle to avoid serialization or locking contention.
 *
 * @param args arguments
 * @param callback completion callback invoked on threadpool worker thread
 * @param cancel_event optional event handle to cancel the attach operation
 * @return true if successfully queued to threadpool, call GetLastError() if false is returned
 */
USBIP_API bool attach_async(
        _In_ const attach_args &args,
        _In_ attach_callback callback,
        _In_opt_ HANDLE cancel_event = HANDLE{});

inline bool attach_async(
        _In_ const device_config &config,
        _In_ attach_callback callback,
        _In_opt_ HANDLE cancel_event = HANDLE{})
{
        return attach_async(attach_args{ config }, std::move(callback), cancel_event);
}

USBIP_API bool attach_async(
        _In_ const attach_args &args,
        _In_ attach_callback callback,
        _In_ std::stop_token stoken);

inline bool attach_async(
        _In_ const device_config &config,
        _In_ attach_callback callback,
        _In_ std::stop_token stoken)
{
        return attach_async(attach_args{ config }, std::move(callback), stoken);
}

/**
 * @param dev handle of the driver device
 * @param location stop attach attempts to this device or stop all active attach attempts if NULL
 * @return number of canceled requests, call GetLastError() if -1 is returned
 */
USBIP_API int stop_attach_attempts(_In_ HANDLE dev, _In_opt_ const device_location *location);

/**
 * Stops attach attempts using a temporary driver handle.
 * @param location stop attach attempts to this device or stop all active attach attempts if NULL
 * @return number of canceled requests, call GetLastError() if -1 is returned
 */
USBIP_API int stop_attach_attempts(_In_opt_ const device_location *location = nullptr);

enum { port_all_closeonly = -2, port_all };

/**
 * @param dev handle of the driver device
 * @param port hub port number starting from 1
 *        port_all - detach all ports
 *        port_all_closeonly - close tcp/ip connections for all ports,
 *              use to skip unnecessary steps on Windows reboot/shutdown
 * @return call GetLastError() if false is returned
 */
USBIP_API bool detach(_In_ HANDLE dev, _In_ int port);

/**
 * Detaches a port using a temporary driver handle.
 * Automatically cleans up any session-isolated volumes before detaching.
 *
 * @param port hub port number starting from 1, or port_all (-1) to detach all ports
 * @return call GetLastError() if false is returned
 */
USBIP_API bool detach(_In_ int port);

inline bool detach_all(_In_ HANDLE dev)
{
        return detach(dev, port_all);
}

inline bool detach_all()
{
        return detach(port_all);
}

using detach_callback = std::function<void(bool success, DWORD error)>;

/**
 * Asynchronously detaches a USB port or all ports using the Windows Threadpool.
 * Automatically cleans up any session-isolated volumes before detaching.
 *
 * @param port hub port number starting from 1, or port_all (-1) to detach all ports
 * @param callback completion callback invoked on threadpool worker thread
 * @param dev optional handle of driver device. If nullptr/invalid, a temporary handle is opened.
 * @return true if successfully queued to threadpool, call GetLastError() if false is returned
 */
USBIP_API bool detach_async(
        _In_ int port,
        _In_ detach_callback callback,
        _In_opt_ HANDLE dev = nullptr);

inline bool detach_all_async(
        _In_ detach_callback callback,
        _In_opt_ HANDLE dev = nullptr)
{
        return detach_async(port_all, std::move(callback), dev);
}

/**
 * Cancel pending synchronous or asynchronous I/O operations on the VHCI driver device.
 * @param dev handle of the driver device
 * @param overlapped optional pointer to the OVERLAPPED structure of a specific request to cancel,
 *                   or nullptr to cancel all pending requests on the handle
 * @return call GetLastError() if false is returned
 */
USBIP_API bool cancel_io(_In_ HANDLE dev, _In_opt_ OVERLAPPED *overlapped = nullptr) noexcept;

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
 * @param data that was read from the device handle
 * @param length data length, must be equal to get_device_state_size()
 * @return state constructed from passed data if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<device_state> get_device_state(_In_ const void *data, _In_ DWORD length);

/**
 * @param dev handle of the driver device that must be opened for serialized I/O
 * @return state that was obtained by read operation on the given handle
           if the result contains a value, otherwise call GetLastError()
 */
USBIP_API std::optional<device_state> read_device_state(_In_ HANDLE dev);

using state_callback = std::function<void(const device_state &state, DWORD error)>;

/**
 * Handle/interface representing an active state monitoring subscription.
 * Cancelling or destroying this object automatically cancels pending I/O and shuts down the monitor.
 */
class state_monitor
{
public:
        virtual ~state_monitor() = default;
        virtual void cancel() noexcept = 0;
};

using state_monitor_ptr = std::shared_ptr<state_monitor>;

/**
 * Asynchronously monitors device state changes emitted by the VHCI driver using the Windows Threadpool.
 * Returns immediately without blocking the caller.
 *
 * The callback is invoked on a threadpool worker thread whenever a device state changes,
 * or with an error code if an unrecoverable failure occurs.
 *
 * Monitoring continues until the returned subscription object is cancelled or destroyed,
 * or until the driver device handle is closed.
 *
 * @param dev handle of the driver device (opened with vhci::open())
 * @param callback callback invoked with device state or error
 * @return shared pointer to the state monitor, or nullptr on error (call GetLastError())
 */
USBIP_API state_monitor_ptr watch_device_states(
        _In_ HANDLE dev,
        _In_ state_callback callback);

/**
 * Asynchronously monitors device state changes using a dedicated driver handle.
 *
 * @param callback callback invoked with device state or error
 * @return shared pointer to the state monitor, or nullptr on error (call GetLastError())
 */
USBIP_API state_monitor_ptr watch_device_states(
        _In_ state_callback callback);

/**
 * RAII high-level client for managing interactions with the VHCI driver.
 */
class client
{
public:
        USBIP_API client();
        USBIP_API explicit client(_In_ bool overlapped);
        USBIP_API explicit client(_In_ Handle dev) noexcept;
        USBIP_API ~client() = default;

        client(const client&) = delete;
        client& operator=(const client&) = delete;
        client(client&&) noexcept = default;
        client& operator=(client&&) noexcept = default;

        USBIP_API explicit operator bool() const noexcept;
        USBIP_API bool operator!() const noexcept;

        USBIP_API HANDLE handle() const noexcept;
        USBIP_API bool open(_In_ bool overlapped = false);
        USBIP_API void close() noexcept;

        // Device queries & persistent devices
        USBIP_API std::optional<std::vector<imported_device>> get_imported_devices() const;
        USBIP_API std::optional<std::vector<device_config>> get_persistent() const;
        USBIP_API bool set_persistent(_In_ const std::vector<device_config> &devices);

        // Attach operations
        USBIP_API int attach(_In_ const attach_args &args);
        inline int attach(_In_ const device_config &config) { return attach(attach_args{ config }); }

        USBIP_API bool attach_async(
                _In_ const attach_args &args,
                _In_ attach_callback callback,
                _In_opt_ HANDLE cancel_event = HANDLE{});

        inline bool attach_async(
                _In_ const device_config &config,
                _In_ attach_callback callback,
                _In_opt_ HANDLE cancel_event = HANDLE{})
        {
                return attach_async(attach_args{ config }, std::move(callback), cancel_event);
        }

        USBIP_API bool attach_async(
                _In_ const attach_args &args,
                _In_ attach_callback callback,
                _In_ std::stop_token stoken);

        inline bool attach_async(
                _In_ const device_config &config,
                _In_ attach_callback callback,
                _In_ std::stop_token stoken)
        {
                return attach_async(attach_args{ config }, std::move(callback), stoken);
        }

        USBIP_API int stop_attach_attempts(_In_opt_ const device_location *location = nullptr);

        // Detach operations
        USBIP_API bool detach(_In_ int port);
        inline bool detach_all() { return detach(port_all); }

        USBIP_API bool detach_async(_In_ int port, _In_ detach_callback callback);
        inline bool detach_all_async(_In_ detach_callback callback)
        {
                return detach_async(port_all, std::move(callback));
        }

        // State monitoring
        USBIP_API state_monitor_ptr watch_device_states(_In_ state_callback callback);

private:
        Handle m_dev;
};

using vhci_client = client;

} // namespace usbip::vhci
