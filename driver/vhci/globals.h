#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <ntdef.h>

typedef struct _GLOBALS
{
	// Path to the driver's Services Key in the registry
	UNICODE_STRING RegistryPath;
} GLOBALS;

extern GLOBALS Globals;

#ifdef __cplusplus
}
#endif
