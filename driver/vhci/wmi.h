#pragma once

#include "pageable.h"
#include "dev.h"

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS reg_wmi(vhci_dev_t *vhci);

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS dereg_wmi(vhci_dev_t *vhci);
