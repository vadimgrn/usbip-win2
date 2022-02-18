#pragma once

#include <wdm.h>

NTSTATUS complete_internal_ioctl(IRP *irp, NTSTATUS status);
