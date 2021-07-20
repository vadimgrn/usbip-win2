#pragma once

#define DRVPREFIX	"usbip_vhci"
#include "dbgcommon.h"

#ifdef DBG

#include "vhci_dev.h"
#include "usbreq.h"
#include "dbgcode.h"

/* NOTE: LSB cannot be used, which is system-wide mask. Thus, DBG_XXX start from 0x0002 */
#define DBG_GENERAL	0x0002
#define DBG_READ	0x0004
#define DBG_WRITE	0x0008
#define DBG_PNP		0x0010
#define DBG_IOCTL	0x0020
#define DBG_POWER	0x0040
#define DBG_WMI		0x0080
#define DBG_URB		0x0100
#define DBG_VDEV	0x0200
#define DBG_ROOT	0x0400
#define DBG_VHCI	0x0800
#define DBG_CPDO	0x1000
#define DBG_HPDO	0x2000
#define DBG_VHUB	0x4000
#define DBG_VPDO	0x8000

const char *dbg_GUID(GUID *guid);

const char *dbg_vdev_type(vdev_type_t type);
const char *dbg_urbr(struct urb_req *urbr);

const char *dbg_vhci_ioctl_code(unsigned int ioctl_code);
const char *dbg_urbfunc(unsigned int urbfunc);

const char *dbg_usb_user_request_code(ULONG code);

#endif	
