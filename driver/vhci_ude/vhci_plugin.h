#pragma once

#include "vhci_dev.h"
#include "usbip_vhci_api.h"

NTSTATUS
plugin_vusb(pctx_vhci_t vhci, WDFREQUEST req, pvhci_pluginfo_t pluginfo);