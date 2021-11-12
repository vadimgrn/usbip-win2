#pragma once

#include "dbgcommon.h"

#ifdef DBG

#include "vhci_dev.h"
#include "usbreq.h"
#include "dbgcode.h"

const char *dbg_GUID(GUID *guid);

const char *dbg_vdev_type(vdev_type_t type);
const char *dbg_urbr(struct urb_req *urbr);

const char *dbg_vhci_ioctl_code(unsigned int ioctl_code);
const char *dbg_urbfunc(unsigned int urbfunc);

const char *dbg_usb_user_request_code(ULONG code);

#endif	
