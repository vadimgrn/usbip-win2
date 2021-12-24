#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_query_device_text(vdev_t *vdev, IRP *irp);

#ifdef __cplusplus
}
#endif
