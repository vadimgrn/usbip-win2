#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS vpdo_get_dsc_from_nodeconn(vpdo_dev_t *vpdo, IRP *irp, USB_DESCRIPTOR_REQUEST *r, ULONG *psize);
PAGEABLE void cache_descriptor(vpdo_dev_t *vpdo, const _URB_CONTROL_DESCRIPTOR_REQUEST *r, const USB_COMMON_DESCRIPTOR *cd);
