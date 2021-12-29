#pragma once

#include "pageable.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_query_id(vdev_t * vdev, PIRP irp); 
