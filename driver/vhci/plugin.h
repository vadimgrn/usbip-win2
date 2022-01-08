#pragma once

#include "pageable.h"
#include "dev.h"
#include "usbip_vhci_api.h"

PAGEABLE NTSTATUS vhci_plugin_vpdo(vhci_dev_t *vhci, vhci_pluginfo_t *pluginfo, ULONG inlen, FILE_OBJECT *fo);
PAGEABLE NTSTATUS vhci_unplug_port(vhci_dev_t *vhci, int port);
