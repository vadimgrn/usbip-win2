#pragma once

#include "usbip_proto.h"

#ifdef DBG

const char *dbg_usbip_hdr(struct usbip_header *hdr);
const char *dbg_command(UINT32 command);

#endif // DBG
