#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_query_capabilities(vdev_t *vdev, IRP *irp, IO_STACK_LOCATION *irpstack);
