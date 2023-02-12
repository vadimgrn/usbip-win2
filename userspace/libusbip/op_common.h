/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

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

inline auto op_status_error(op_status_t st)
{
        static const UINT32 v[] = 
        {
                0, // ST_OK
                ERROR_USBIP_ST_NA,
                ERROR_USBIP_ST_DEV_BUSY,
                ERROR_USBIP_ST_DEV_ERR,
                ERROR_USBIP_ST_NODEV,
                ERROR_USBIP_ST_ERROR,
        };

        return st >= 0 && st < sizeof(v)/sizeof(*v) ? v[st] : ERROR_USBIP_ST_ERROR;
}

} // namespace usbip
