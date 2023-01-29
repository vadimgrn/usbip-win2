#pragma once

constexpr auto& usbip_port = "3240";

enum op_status_t // op_common.status
{
        ST_OK,
        ST_NA, // !SDEV_ST_AVAILABLE
        ST_DEV_BUSY, // SDEV_ST_USED
        ST_DEV_ERR, // SDEV_ST_ERROR
        ST_NODEV, // requested device not found by busid
        ST_ERROR // ST_DEV_ERR?
};

/* error codes for the driver ioctls */
enum err_t
{
        ERR_PORTFULL = -6,
        ERR_PROTOCOL,
        ERR_VERSION,
        ERR_NETWORK,
        ERR_CONNECT,
        ERR_GENERAL,
        ERR_NONE
};

static_assert(!ERR_NONE);

enum 
{ 
        USBIP_VERSION = 0x111, 
        USBIP_DEV_PATH_MAX = 256, 
        USBIP_BUS_ID_SIZE = 32 
};
