#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE BOOLEAN process_pnp_vpdo(pvpdo_dev_t vpdo, PIRP irp, PIO_STACK_LOCATION irpstack);