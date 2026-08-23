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

/*
 * Strings encoding is UTF8.
 */

namespace usbip
{

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
enum class receive_mode { zero_copy, low_latency };

struct device_location
{
        std::string hostname;
        std::string service; ///< TCP/IP port number or symbolic name
        std::string busid;
};

/**
 * @see generate_device_serial
 * @see validate_device_serial
 * @see get_device_serial_maxlen
 */
struct persistent_device
{
        device_location location;
        std::string serial; ///< optional device serial number if you want to set/override it
        receive_mode recv_mode = receive_mode::zero_copy; ///< how to optimize data reception over the network
        bool once{}; ///< do not start automatic attach attempts if cannot connect; for attach_args only
};

struct imported_device
{
        device_location location;
        int port{}; ///< hub port number, >= 1

        UINT32 devid{};
        USB_DEVICE_SPEED speed = UsbLowSpeed;

        UINT16 vendor{};
        UINT16 product{};

        std::string serial; ///< only filled if you set it in attach_args
        receive_mode recv_mode = receive_mode::zero_copy; ///< @see attach_args.recv_mode
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

using attach_args = persistent_device;

/**
 * @param dev handle of the driver device
 * @param args arguments
 * @return hub port number, >= 1. Call GetLastError() if zero is returned.
 */
USBIP_API int attach(_In_ HANDLE dev, _In_ const attach_args &args);

/**
 * @param dev handle of the driver device
 * @param location stop attach attempts to this device or stop all active attach attempts if NULL
 * @return number of canceled requests, call GetLastError() if -1 is returned
 */
USBIP_API int stop_attach_attempts(_In_ HANDLE dev, _In_opt_ const device_location *location);

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

} // namespace usbip::vhci
