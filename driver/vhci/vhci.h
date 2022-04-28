#pragma once

#include <ntdef.h>

const ULONG USBIP_VHCI_POOL_TAG = 'ICHV';

struct GLOBALS
{
	UNICODE_STRING RegistryPath; // Path to the driver's Services Key in the registry
};

extern GLOBALS Globals;
