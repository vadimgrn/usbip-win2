#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_query_resource_requirements(vdev_t * vdev, PIRP irp);
PAGEABLE NTSTATUS pnp_query_resources(vdev_t * vdev, PIRP irp);
PAGEABLE NTSTATUS pnp_filter_resource_requirements(vdev_t * vdev, PIRP irp);
