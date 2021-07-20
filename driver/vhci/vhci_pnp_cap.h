#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_query_capabilities(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack);
