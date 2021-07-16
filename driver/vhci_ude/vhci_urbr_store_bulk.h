#pragma once

#include "vhci_urbr.h"

NTSTATUS
store_urbr_bulk_partial(WDFREQUEST req_read, purb_req_t urbr);

NTSTATUS
store_urbr_bulk(WDFREQUEST req_read, purb_req_t urbr);

