#pragma once

#include "vhci_driver.h"

PAGEABLE NTSTATUS
evt_add_vhci(_In_ WDFDRIVER drv, _Inout_ PWDFDEVICE_INIT dinit);
