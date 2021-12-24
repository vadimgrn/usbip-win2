#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_query_id(pvdev_t vdev, PIRP irp); 

#ifdef __cplusplus
}
#endif
