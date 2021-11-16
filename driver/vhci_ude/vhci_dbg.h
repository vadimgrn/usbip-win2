#pragma once

#include "vhci_urbr.h"

extern char buf_dbg_setup_packet[];
extern char buf_dbg_urbr[];

extern unsigned int len_dbg_setup_packet;
extern unsigned int len_dbg_urbr;

const char *dbg_usb_setup_packet(PCUCHAR packet);
const char *dbg_urbr(const urb_req_t *urbr);
