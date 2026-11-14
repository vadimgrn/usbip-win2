/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

namespace usbip
{

inline constexpr auto& tcp_port = "3240";
inline constexpr auto& driver_filename = L"usbip2_ude"; // used by filter driver
inline constexpr auto& persistent_devices_value_name = L"PersistentDevices";

enum op_status_t // op_common.status
{
        ST_OK,
        ST_NA, // !SDEV_ST_AVAILABLE
        ST_DEV_BUSY, // SDEV_ST_USED
        ST_DEV_ERR, // SDEV_ST_ERROR
        ST_NODEV, // requested device not found by busid
        ST_ERROR // ST_DEV_ERR?
};

/*
 * SERIAL_BUFSZ
 *      buffer length for USB_DEVICE_DESCRIPTOR.iSerialNumber
 *      string in characters, including the terminating '\0'.
 *
 * MAX_HUB_PORTS
 *      Hard limit for USB root hub ports (USB 2.0 / USB 3.x Hub Descriptor
 *      bNbrPorts is 1 byte, UINT8). UdeCx (UdecxWdfDeviceAddUsbDeviceEmulation)
 *      enforces NumberOfUsb20Ports + NumberOfUsb30Ports <= 255 and returns
 *      STATUS_INVALID_PARAMETER if exceeded.
 */
enum {
        USBIP_VERSION = 0x111, // protocol
        DEV_PATH_MAX = 256,
        BUS_ID_SIZE = 32,
        SERIAL_BUFSZ = 16,
        MAX_HUB_PORTS = 255,
};

enum {
        max_device_id_len = 200, // MAX_DEVICE_ID_LEN (cfgmgr32.h)
        se_group_logon_id = 0xC0000000UL, // SE_GROUP_LOGON_ID (winnt.h)
};

namespace session
{
        constexpr auto system = 0UL;
        constexpr auto invalid = ~system;
}

} // namespace usbip
