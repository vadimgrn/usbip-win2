#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_remove_device(pvdev_t vdev, PIRP irp);
