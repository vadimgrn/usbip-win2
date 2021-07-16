#pragma once

#include "vhci_urbr.h"

NTSTATUS
store_urbr_reset_pipe(WDFREQUEST req_read, purb_req_t urbr);
