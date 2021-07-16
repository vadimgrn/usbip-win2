#pragma once

#include "vhci_urbr.h"

NTSTATUS
store_urbr_control_transfer_partial(WDFREQUEST req_read, purb_req_t urbr);

NTSTATUS
store_urbr_control_transfer_ex_partial(WDFREQUEST req_read, purb_req_t urbr);

NTSTATUS
store_urbr_control_transfer(WDFREQUEST req_read, purb_req_t urbr);

NTSTATUS
store_urbr_control_transfer_ex(WDFREQUEST req_read, purb_req_t urbr);

