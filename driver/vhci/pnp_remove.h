#pragma once

#include "pageable.h"
#include "dev.h"

PAGEABLE NTSTATUS pnp_remove_device(vdev_t * vdev, PIRP irp);
