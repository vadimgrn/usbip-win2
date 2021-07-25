#pragma once

#include "vhci_dev.h"

pctx_vusb_t get_vusb(pctx_vhci_t vhci, ULONG port);
pctx_vusb_t get_vusb_by_req(WDFREQUEST req);

void put_vusb(pctx_vusb_t vusb);
void put_vusb_passively(pctx_vusb_t vusb);

