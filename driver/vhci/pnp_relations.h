#pragma once

#include "pageable.h"
#include <ntdef.h>

struct _IRP;
struct vdev_t;

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS pnp_query_device_relations(vdev_t *vdev, _IRP *irp);
