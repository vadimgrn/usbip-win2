#pragma once

#include <ntdef.h>

struct GLOBALS
{
	UNICODE_STRING RegistryPath; // Path to the driver's Services Key in the registry
};

extern GLOBALS Globals;
