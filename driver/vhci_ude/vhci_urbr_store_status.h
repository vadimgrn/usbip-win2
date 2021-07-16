#pragma once

#include "vhci_urbr.h"

NTSTATUS
store_urbr_get_status(WDFREQUEST req_read, purb_req_t urbr);

