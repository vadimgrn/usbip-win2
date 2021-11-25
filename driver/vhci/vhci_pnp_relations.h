#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_query_device_relations(pvdev_t vdev, PIRP irp);
