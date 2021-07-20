#pragma once

#include "vhci_dev.h"

NTSTATUS vhci_ioctl_abort_pipe(pvpdo_dev_t vpdo, USBD_PIPE_HANDLE hPipe);
NTSTATUS vhci_internal_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP Irp);