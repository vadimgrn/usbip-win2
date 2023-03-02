#pragma once

namespace usbip
{

constexpr auto &tcp_port = "3240";

enum op_status_t // op_common.status
{
        ST_OK,
        ST_NA, // !SDEV_ST_AVAILABLE
        ST_DEV_BUSY, // SDEV_ST_USED
        ST_DEV_ERR, // SDEV_ST_ERROR
        ST_NODEV, // requested device not found by busid
        ST_ERROR // ST_DEV_ERR?
};

enum { 
        USBIP_VERSION = 0x111, // protocol
        DEV_PATH_MAX = 256, 
        BUS_ID_SIZE = 32 
};

} // namespace usbip
