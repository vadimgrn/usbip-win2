#pragma once

#include <wdm.h>

const auto USBIP_VHCI_POOL_TAG = (ULONG)'VhcI';
extern NPAGED_LOOKASIDE_LIST g_lookaside;
