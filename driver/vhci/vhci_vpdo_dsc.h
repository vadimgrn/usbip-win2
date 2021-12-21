#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS vpdo_get_dsc_from_nodeconn(pvpdo_dev_t vpdo, PIRP irp, PUSB_DESCRIPTOR_REQUEST dsc_req, PULONG psize);
PAGEABLE void try_to_cache_descriptor(pvpdo_dev_t vpdo, struct _URB_CONTROL_DESCRIPTOR_REQUEST* urb_cdr, const USB_COMMON_DESCRIPTOR *dsc);
