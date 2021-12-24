#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include "usbreq.h"

NTSTATUS store_urbr(PIRP irp, struct urb_req *urbr);
PAGEABLE NTSTATUS vhci_read(__in PDEVICE_OBJECT devobj, __in PIRP irp);

#ifdef __cplusplus
}
#endif
