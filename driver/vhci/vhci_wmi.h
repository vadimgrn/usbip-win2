#pragma once

#include "pageable.h"
#include "vhci_dev.h"

PAGEABLE NTSTATUS reg_wmi(vhci_dev_t * vhci);
PAGEABLE NTSTATUS dereg_wmi(vhci_dev_t * vhci);
