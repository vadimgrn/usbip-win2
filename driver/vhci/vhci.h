#pragma once

#include <ntddk.h>
#include <ntstrsafe.h>
#include <initguid.h> // required for GUID definitions

#include "basetype.h"
#include "strutil.h"
#include "vhci_dbg.h"

#define USBIP_VHCI_POOL_TAG (ULONG) 'VhcI'

/* NOTE: a trailing string null character is included */
#define WTEXT_LEN(wtext)	(sizeof(wtext) / sizeof(WCHAR))

extern NPAGED_LOOKASIDE_LIST g_lookaside;
