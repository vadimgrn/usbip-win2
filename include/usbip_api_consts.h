#pragma once

#include <assert.h>

enum op_status_t // op_common.status
{
        ST_OK,
        ST_NA, /* Device requested for import is not available */
        ST_DEV_BUSY, /* Device requested for import is in error state */
        ST_DEV_ERR,
        ST_NODEV,
        ST_ERROR
};

/* error codes for userspace tools and library */
enum err_t
{
        ERR_CERTIFICATE	= -12,
        ERR_ACCESS,
        ERR_PORTFULL,
        ERR_DRIVER,
        ERR_NOTEXIST,
        ERR_EXIST,
        ERR_STATUS,
        ERR_PROTOCOL,
        ERR_VERSION,
        ERR_NETWORK,
        ERR_INVARG,
        ERR_GENERAL,
        ERR_NONE
};

static_assert(!ERR_NONE, "assert");

enum usbip_device_status 
{
	/* dev status unknown. */
	DEV_ST_UNKNOWN,

	/* sdev is available. */
	SDEV_ST_AVAILABLE,
	/* sdev is now used. */
	SDEV_ST_USED,
	/* sdev is unusable because of a fatal error. */
	SDEV_ST_ERROR,

	/* vdev does not connect a remote device. */
	VDEV_ST_NULL,
	/* vdev is used, but the USB address is not assigned yet */
	VDEV_ST_NOTASSIGNED,
	VDEV_ST_USED,
	VDEV_ST_ERROR
};

enum 
{ 
        USBIP_VERSION = 0x111, 
        USBIP_DEV_PATH_MAX = 256, 
        USBIP_BUS_ID_SIZE = 32 
};
