#pragma once

#include "vhci_dev.h"

void try_to_cache_descriptor(pvpdo_dev_t vpdo, struct _URB_CONTROL_DESCRIPTOR_REQUEST* urb_cdr, USB_COMMON_DESCRIPTOR *dsc);