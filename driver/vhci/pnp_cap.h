#pragma once

#include "pageable.h"
#include "dev.h"

PAGEABLE NTSTATUS pnp_query_capabilities(vdev_t *vdev, IRP *irp);
