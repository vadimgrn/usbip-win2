#pragma once

#include "vhci_urbr.h"

enum { DBG_URBR_BUFSZ = 64 };
const char *dbg_urbr(char* buf, unsigned int len, const urb_req_t *urbr);
