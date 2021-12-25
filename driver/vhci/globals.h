#pragma once

#include <ntdef.h>

typedef struct _GLOBALS
{
	// Path to the driver's Services Key in the registry
	UNICODE_STRING RegistryPath;
} GLOBALS;

extern GLOBALS Globals;
