#pragma once

#include "pageable.h"
#include "dev.h"

PAGEABLE NTSTATUS pnp_query_interface(vdev_t *vdev, IRP *irp);
