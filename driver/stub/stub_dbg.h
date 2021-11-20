#pragma once

#include "dbgcommon.h"
#include "stub_dev.h"
#include "stub_devconf.h"

#define CSPKT_DIRECTION(csp)		((csp)->bmRequestType.Dir)
#define CSPKT_REQUEST_TYPE(csp)		((csp)->bmRequestType.Type)
#define CSPKT_RECIPIENT(csp)		((csp)->bmRequestType.Recipient)
#define CSPKT_REQUEST(csp)		((csp)->bRequest)
#define CSPKT_DESCRIPTOR_TYPE(csp)	((csp)->wValue.HiByte)
#define CSPKT_DESCRIPTOR_INDEX(csp)	((csp)->wValue.LowByte)

#define CSPKT_IS_IN(csp)		(CSPKT_DIRECTION(csp) == BMREQUEST_DEVICE_TO_HOST)

enum { DBG_DEVICE_BUFSZ = 36 };
const char *dbg_device(char *buf, unsigned int len, const DEVICE_OBJECT *devobj);

enum { DBG_DEVICES_BUFSZ = 768 };
const char *dbg_devices(char *buf, unsigned int len, const DEVICE_OBJECT *devobj, bool is_attached);

enum { DBG_DEVSTUB_BUFSZ = 300 };
const char *dbg_devstub(char *buf, unsigned int len, const usbip_stub_dev_t *devstub);

const char *dbg_stub_ioctl_code(int ioctl_code);
