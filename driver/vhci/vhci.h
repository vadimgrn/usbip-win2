#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <ntddk.h>
#include <ntstrsafe.h>
#include <initguid.h> // required for GUID definitions

#include "basetype.h"
#include "strutil.h"

#define USBIP_VHCI_POOL_TAG (ULONG) 'VhcI'

extern NPAGED_LOOKASIDE_LIST g_lookaside;

#ifdef __cplusplus
}
#endif
