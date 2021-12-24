#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_query_interface(vdev_t *vdev, IRP *irp);

#ifdef __cplusplus
}
#endif
