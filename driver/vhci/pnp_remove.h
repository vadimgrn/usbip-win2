#pragma once

#include <wdm.h>
#include "pageable.h"

struct _IRP;
struct vdev_t;

extern const ULONG WskEvents[2];

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void destroy_device(vdev_t *vdev);

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS pnp_remove_device(vdev_t *vdev, _IRP *irp);


