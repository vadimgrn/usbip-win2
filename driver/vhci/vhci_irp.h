#pragma once

#include "basetype.h"
#include <wdm.h>

PAGEABLE NTSTATUS irp_pass_down(PDEVICE_OBJECT devobj, PIRP irp);
PAGEABLE NTSTATUS irp_send_synchronously(PDEVICE_OBJECT devobj, PIRP irp);

NTSTATUS irp_done(PIRP irp, NTSTATUS status);
NTSTATUS irp_success(PIRP irp);