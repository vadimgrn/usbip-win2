#pragma once

#include <libdrv\pageable.h>
#include <ntdef.h>

struct _IRP;
struct vdev_t;

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void destroy_device(_In_opt_ vdev_t *vdev);

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS pnp_remove_device(vdev_t *vdev, _IRP *irp);


