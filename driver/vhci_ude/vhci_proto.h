#pragma once

#include "vhci_dev.h"
#include <stdbool.h>

struct usbip_header;

void
set_cmd_submit_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid,
			    bool dir_in, pctx_ep_t ep, ULONG TransferFlags, ULONG TransferBufferLength);

void
set_cmd_unlink_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid, unsigned long seqnum_unlink);