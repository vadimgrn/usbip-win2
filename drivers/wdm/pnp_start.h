#pragma once

#include <libdrv\pageable.h>
#include <ntdef.h>

struct _IRP;
struct vdev_t;

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS pnp_start_device(vdev_t *vdev, _IRP *irp);
