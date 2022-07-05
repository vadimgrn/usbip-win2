#pragma once

#include "pageable.h"
#include <wdm.h>

_Function_class_(DRIVER_ADD_DEVICE)
_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
extern "C" PAGEABLE NTSTATUS vhci_add_device(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT pdo);
