#pragma once

#include "basetype.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS reg_wmi(pvhci_dev_t vhci);
PAGEABLE NTSTATUS dereg_wmi(pvhci_dev_t vhci);
