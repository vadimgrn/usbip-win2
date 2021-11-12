#pragma once

#include "dbgcommon.h"
#include "dbgcode.h"
#include "stub_dev.h"

#ifdef DBG

#include "stub_devconf.h"

const char *dbg_device(PDEVICE_OBJECT devobj);
const char *dbg_devices(PDEVICE_OBJECT devobj, BOOLEAN is_attached);
const char *dbg_devstub(usbip_stub_dev_t *devstub);

const char *dbg_stub_ioctl_code(ULONG ioctl_code);

#endif // DBG
