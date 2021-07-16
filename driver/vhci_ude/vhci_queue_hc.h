#pragma once

#include "vhci_driver.h"

PAGEABLE NTSTATUS
create_queue_hc(pctx_vhci_t vhci);
