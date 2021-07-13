#pragma once

#include "vhci_dev.h"

struct usbip_header;

unsigned int
transflag(unsigned int flags);

void
set_cmd_submit_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid,
			    unsigned int direct, pctx_ep_t ep, unsigned int flags, unsigned int len);
void
set_cmd_unlink_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid, unsigned long seqnum_unlink);