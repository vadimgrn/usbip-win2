#pragma once

#include "pageable.h"
#include "dev.h"

PAGEABLE NTSTATUS pnp_query_device_relations(vdev_t * vdev, PIRP irp);
