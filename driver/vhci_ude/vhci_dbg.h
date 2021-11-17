#pragma once

#include "vhci_urbr.h"

extern char buf_dbg_urbr[];
extern unsigned int len_dbg_urbr;

enum { DBG_USB_SETUP_BUFLEN = 64 };
const char *dbg_usb_setup_packet(char *buf, unsigned int len, const void *packet);

const char *dbg_urbr(const urb_req_t *urbr);
