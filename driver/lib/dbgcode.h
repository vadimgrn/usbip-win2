#pragma once

#include <ntddk.h>
#include <usb.h>

#include "namecode.h"

const char *dbg_namecode(namecode_t *namecodes, const char *codetype, unsigned int code);
const char *dbg_namecode_buf(namecode_t *namecodes, const char *codetype, unsigned int code, char *buf, int buf_max);
const char *dbg_usbd_status(USBD_STATUS status);
const char *dbg_dispatch_major(UCHAR major);
const char *dbg_pnp_minor(UCHAR minor);
const char *dbg_wmi_minor(UCHAR minor);
const char *dbg_power_minor(UCHAR minor);
const char *dbg_system_power(SYSTEM_POWER_STATE state);
const char *dbg_device_power(DEVICE_POWER_STATE state);
const char *dbg_usb_descriptor_type(UCHAR dsc_type);
