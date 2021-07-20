#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS pnp_start_device(pvdev_t vdev, PIRP irp);