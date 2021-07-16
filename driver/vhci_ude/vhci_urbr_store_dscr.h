#pragma once

#include "vhci_urbr.h"

NTSTATUS
store_urbr_dscr_dev(WDFREQUEST req_read, purb_req_t urbr);

NTSTATUS
store_urbr_dscr_intf(WDFREQUEST req_read, purb_req_t urbr);
