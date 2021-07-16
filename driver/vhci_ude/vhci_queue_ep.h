#pragma once

#include "vhci_dev.h"

WDFQUEUE
create_queue_ep(pctx_ep_t ep);
