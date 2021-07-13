#pragma once

#include "vhci_urbr.h"

NTSTATUS
store_urbr_select_config(WDFREQUEST req_read, purb_req_t urbr);

NTSTATUS
store_urbr_select_interface(WDFREQUEST req_read, purb_req_t urbr);


