/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

 /*
  * This header is used in the driver and libusbip.
  */

#include <usbip\consts.h>
#include <resources\messages.h>

#include <basetsd.h>

namespace usbip
{

inline auto op_status_str(op_status_t st)
{
        static const char* v[] = {
                "ST_OK", "ST_NA", "ST_DEV_BUSY", "ST_DEV_ERR", "ST_NODEV", "ST_ERROR"
        };

        return st >= 0 && st < sizeof(v)/sizeof(*v) ? v[st] : "op_status_str: out of range";
}

inline auto op_status_error(_In_ op_status_t st)
{
        const USBIP_STATUS v[] = 
        {
                USBIP_ERROR_SUCCESS, // ST_OK
                USBIP_ERROR_ST_NA,
                USBIP_ERROR_ST_DEV_BUSY,
                USBIP_ERROR_ST_DEV_ERR,
                USBIP_ERROR_ST_NODEV,
                USBIP_ERROR_ST_ERROR,
        };

        return st >= 0 && st < sizeof(v)/sizeof(*v) ? v[st] : USBIP_ERROR_ST_ERROR;
}

} // namespace usbip
