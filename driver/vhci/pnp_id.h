#pragma once

#include "pageable.h"
#include <ntdef.h>

struct _IRP;
struct vdev_t;

PAGEABLE NTSTATUS pnp_query_id(vdev_t *vdev, _IRP *irp); 
