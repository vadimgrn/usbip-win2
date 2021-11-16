#pragma once

#include <ntddk.h>
#include <usb.h>

#include "namecode.h"

const char* dbg_namecode_buf(
        const namecode_t* namecodes, const char* codetype, unsigned int code,
        char* buf, unsigned int buf_max, unsigned int* written);

const char *dbg_namecode(const namecode_t *namecodes, const char *codetype, unsigned int code);

const char *dbg_usbd_status(USBD_STATUS status);

