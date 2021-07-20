#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_query_device_text(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack);