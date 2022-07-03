#pragma once

#include "pageable.h"
#include "dev.h"

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS pnp_query_interface(vdev_t *vdev, IRP *irp);
