#pragma once

#include "basetype.h"
#include "vhci_dev.h"

#include <wdmguid.h>
#include <usbbusif.h>

BOOLEAN USB_BUSIFFN IsDeviceHighSpeed(PVOID context);
PAGEABLE NTSTATUS pnp_query_interface(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack);
