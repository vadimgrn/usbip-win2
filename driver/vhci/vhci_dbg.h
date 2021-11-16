#pragma once

#include "dbgcommon.h"

#include "vhci_dev.h"
#include "usbreq.h"
#include "dbgcode.h"

const char *dbg_urbr(struct urb_req *urbr);
const char *dbg_vhci_ioctl_code(unsigned int ioctl_code);

