#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include "vhci_dev.h"
#include "usbip_vhci_api.h"

PAGEABLE NTSTATUS vhci_plugin_vpdo(pvhci_dev_t vhci, pvhci_pluginfo_t pluginfo, ULONG inlen, PFILE_OBJECT fo);
PAGEABLE NTSTATUS vhci_unplug_port(pvhci_dev_t vhci, CHAR port);

#ifdef __cplusplus
}
#endif
