#pragma once

#include <windows.h>
#include <setupapi.h>

#include "usbip_setupdi.h"

#define STUB_DRIVER_SVCNAME	"usbip_stub"

BOOL is_service_usbip_stub(HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data);

int attach_stub_driver(devno_t devno);
int detach_stub_driver(devno_t devno);