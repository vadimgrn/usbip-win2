#pragma once

#include "pageable.h"
#include "dev.h"

PAGEABLE NTSTATUS pnp_query_device_text(vdev_t *vdev, IRP *irp);
