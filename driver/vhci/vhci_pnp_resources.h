#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_query_resource_requirements(pvdev_t vdev, PIRP irp);
PAGEABLE NTSTATUS pnp_query_resources(pvdev_t vdev, PIRP irp);
PAGEABLE NTSTATUS pnp_filter_resource_requirements(pvdev_t vdev, PIRP irp);

#ifdef __cplusplus
}
#endif
